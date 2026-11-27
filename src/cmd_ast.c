#include "cmd_ast.h"
#include "cmd_msg.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wctype.h>

typedef struct {
    const wchar_t *src;
    size_t pos;
    int paren_depth;
    wchar_t *err_buf;
    size_t err_cap;
    bool has_error;
    bool silent_err;
} parser_t;

static void set_parse_err_msg(parser_t *p, DWORD msg_id, const wchar_t *fallback, const wchar_t *arg1) {
    if (p->has_error) return;
    p->has_error = true;
    if (p->err_buf && p->err_cap > 0) {
        cmd_msg_format_buf(msg_id, fallback, arg1, NULL, p->err_buf, p->err_cap);
    }
}

static void set_parse_err(parser_t *p, const wchar_t *msg) {
    (void)msg;
    set_parse_err_msg(p, CMD_MSG_SYNTAX_ERR, L"The syntax of the command is incorrect.\r\n", NULL);
}

static bool is_hws(wchar_t c) {
    return c == L' ' || c == L'\t';
}

static bool is_ws(wchar_t c) {
    return c == L' ' || c == L'\t' || c == L'\r' || c == L'\n';
}

static void skip_hws(parser_t *p) {
    while (is_hws(p->src[p->pos])) p->pos++;
}

static void skip_ws(parser_t *p) {
    while (is_ws(p->src[p->pos])) p->pos++;
}

static cmd_node_t *alloc_node(cmd_node_kind_t kind) {
    cmd_node_t *n = (cmd_node_t *)calloc(1, sizeof(cmd_node_t));
    if (n) n->kind = kind;
    return n;
}

static void free_redirs(cmd_redir_t *r) {
    while (r) {
        cmd_redir_t *nx = r->next;
        free(r->target);
        free(r);
        r = nx;
    }
}

void cmd_ast_free(cmd_node_t *node) {
    if (!node) return;
    free_redirs(node->redirs);
    switch (node->kind) {
        case CMD_NODE_SIMPLE:
            free(node->u.simple.raw_text);
            free(node->u.simple.cmd_word);
            free(node->u.simple.raw_tail);
            break;
        case CMD_NODE_SEQ:
        case CMD_NODE_AND:
        case CMD_NODE_OR:
        case CMD_NODE_PIPE:
            cmd_ast_free(node->u.bin.left);
            cmd_ast_free(node->u.bin.right);
            break;
        case CMD_NODE_BLOCK:
            cmd_ast_free(node->u.block.body);
            break;
        case CMD_NODE_IF:
            free(node->u.if_stmt.lhs);
            free(node->u.if_stmt.rhs);
            cmd_ast_free(node->u.if_stmt.then_branch);
            cmd_ast_free(node->u.if_stmt.else_branch);
            break;
        case CMD_NODE_FOR:
            free(node->u.for_stmt.opts_or_root);
            free(node->u.for_stmt.set_spec);
            cmd_ast_free(node->u.for_stmt.body);
            break;
        case CMD_NODE_REM:
            break;
    }
    free(node);
}

static void append_redir(cmd_redir_t **head, cmd_redir_t *r) {
    if (!*head) {
        *head = r;
        return;
    }
    cmd_redir_t *cur = *head;
    while (cur->next) cur = cur->next;
    cur->next = r;
}

static bool match_kw_ci(const wchar_t *s, const wchar_t *kw, size_t *out_len) {
    size_t klen = wcslen(kw);
    if (_wcsnicmp(s, kw, klen) != 0) return false;
    wchar_t nx = s[klen];
    if (nx == L'\0' || is_ws(nx) || nx == L'(' || nx == L')' || nx == L'/' || nx == L'&' || nx == L'|') {
        if (out_len) *out_len = klen;
        return true;
    }
    return false;
}

/* Reads a single unquoted-delimited or quoted token (preserving or stripping quotes as requested). */
static wchar_t *read_cond_token(parser_t *p, bool strip_quotes, bool stop_on_eq) {
    skip_ws(p);
    size_t cap = 64, len = 0;
    wchar_t *buf = (wchar_t *)malloc(cap * sizeof(wchar_t));
    if (!buf) return NULL;
    bool in_q = false;
    while (p->src[p->pos]) {
        wchar_t c = p->src[p->pos];
        if (c == L'"') {
            in_q = !in_q;
            if (!strip_quotes) {
                if (len + 2 >= cap) { cap *= 2; buf = (wchar_t *)realloc(buf, cap * sizeof(wchar_t)); if (!buf) return NULL; }
                buf[len++] = c;
            }
            p->pos++;
            continue;
        }
        if (!in_q) {
            if (is_ws(c) || c == L'&' || c == L'|' || c == L'<' || c == L'>') break;
            if (p->paren_depth > 0 && c == L')') break;
            if (stop_on_eq && c == L'=' && p->src[p->pos + 1] == L'=') break;
            if (c == L'^' && p->src[p->pos + 1]) {
                p->pos++;
                c = p->src[p->pos];
            }
        }
        if (len + 2 >= cap) { cap *= 2; buf = (wchar_t *)realloc(buf, cap * sizeof(wchar_t)); if (!buf) return NULL; }
        buf[len++] = c;
        p->pos++;
    }
    buf[len] = L'\0';
    return buf;
}

static wchar_t *read_redir_target(parser_t *p) {
    return read_cond_token(p, true, false);
}

static bool try_parse_redir(parser_t *p, cmd_redir_t **head) {
    size_t save = p->pos;
    int src_fd = -1;
    wchar_t c = p->src[p->pos];
    if (c >= L'0' && c <= L'9' && (p->src[p->pos + 1] == L'>' || p->src[p->pos + 1] == L'<')) {
        src_fd = c - L'0';
        p->pos++;
        c = p->src[p->pos];
    }
    if (c != L'<' && c != L'>') {
        p->pos = save;
        return false;
    }
    if (c == L'<') {
        p->pos++;
        if (src_fd < 0) src_fd = 0;
        wchar_t *tgt = read_redir_target(p);
        if (!tgt || !*tgt) {
            free(tgt);
            set_parse_err(p, L"The syntax of the command is incorrect.");
            return true;
        }
        cmd_redir_t *r = (cmd_redir_t *)calloc(1, sizeof(cmd_redir_t));
        if (r) {
            r->kind = CMD_REDIR_IN;
            r->src_fd = src_fd;
            r->target = tgt;
            append_redir(head, r);
        } else {
            free(tgt);
        }
        return true;
    }

    /* '>' */
    p->pos++;
    if (src_fd < 0) src_fd = 1;
    if (p->src[p->pos] == L'&' && p->src[p->pos + 1] >= L'0' && p->src[p->pos + 1] <= L'9') {
        int dst_fd = p->src[p->pos + 1] - L'0';
        p->pos += 2;
        cmd_redir_t *r = (cmd_redir_t *)calloc(1, sizeof(cmd_redir_t));
        if (r) {
            r->kind = CMD_REDIR_DUP;
            r->src_fd = src_fd;
            r->dst_fd = dst_fd;
            append_redir(head, r);
        }
        return true;
    }

    bool append = false;
    if (p->src[p->pos] == L'>') {
        append = true;
        p->pos++;
    }
    wchar_t *tgt = read_redir_target(p);
    if (!tgt || !*tgt) {
        free(tgt);
        set_parse_err(p, L"The syntax of the command is incorrect.");
        return true;
    }
    cmd_redir_t *r = (cmd_redir_t *)calloc(1, sizeof(cmd_redir_t));
    if (r) {
        r->kind = append ? CMD_REDIR_OUT_APPEND : CMD_REDIR_OUT_TRUNC;
        r->src_fd = src_fd;
        r->target = tgt;
        append_redir(head, r);
    } else {
        free(tgt);
    }
    return true;
}

static void parse_trailing_redirs(parser_t *p, cmd_redir_t **head) {
    for (;;) {
        skip_ws(p);
        size_t save = p->pos;
        if (!try_parse_redir(p, head) || p->has_error || p->pos == save) {
            break;
        }
    }
}

static cmd_node_t *parse_seq(parser_t *p);
static cmd_node_t *parse_stmt(parser_t *p);

static cmd_node_t *parse_if(parser_t *p) {
    p->pos += 2; /* skip "if" */
    cmd_node_t *node = alloc_node(CMD_NODE_IF);
    if (!node) return NULL;

    for (;;) {
        skip_ws(p);
        if (_wcsnicmp(p->src + p->pos, L"/i", 2) == 0 &&
            (p->src[p->pos + 2] == L'\0' || is_ws(p->src[p->pos + 2]))) {
            node->u.if_stmt.case_insensitive = true;
            p->pos += 2;
            continue;
        }
        size_t kwl = 0;
        if (match_kw_ci(p->src + p->pos, L"not", &kwl)) {
            node->u.if_stmt.negated = !node->u.if_stmt.negated;
            p->pos += kwl;
            continue;
        }
        break;
    }

    skip_ws(p);
    size_t kwl = 0;
    if (match_kw_ci(p->src + p->pos, L"exist", &kwl)) {
        p->pos += kwl;
        node->u.if_stmt.cond = IF_COND_EXIST;
        node->u.if_stmt.lhs = read_cond_token(p, true, false);
    } else if (match_kw_ci(p->src + p->pos, L"defined", &kwl)) {
        p->pos += kwl;
        node->u.if_stmt.cond = IF_COND_DEFINED;
        node->u.if_stmt.lhs = read_cond_token(p, true, false);
    } else if (match_kw_ci(p->src + p->pos, L"errorlevel", &kwl)) {
        p->pos += kwl;
        node->u.if_stmt.cond = IF_COND_ERRORLEVEL;
        node->u.if_stmt.lhs = read_cond_token(p, true, false);
    } else if (match_kw_ci(p->src + p->pos, L"cmdextversion", &kwl)) {
        p->pos += kwl;
        node->u.if_stmt.cond = IF_COND_CMDEXTVERSION;
        node->u.if_stmt.lhs = read_cond_token(p, true, false);
    } else {
        node->u.if_stmt.lhs = read_cond_token(p, false, true);
        skip_ws(p);
        if (p->src[p->pos] == L'=' && p->src[p->pos + 1] == L'=') {
            p->pos += 2;
            node->u.if_stmt.cond = IF_COND_EQ_STR;
        } else if (match_kw_ci(p->src + p->pos, L"equ", &kwl)) {
            p->pos += kwl; node->u.if_stmt.cond = IF_COND_EQU;
        } else if (match_kw_ci(p->src + p->pos, L"neq", &kwl)) {
            p->pos += kwl; node->u.if_stmt.cond = IF_COND_NEQ;
        } else if (match_kw_ci(p->src + p->pos, L"lss", &kwl)) {
            p->pos += kwl; node->u.if_stmt.cond = IF_COND_LSS;
        } else if (match_kw_ci(p->src + p->pos, L"leq", &kwl)) {
            p->pos += kwl; node->u.if_stmt.cond = IF_COND_LEQ;
        } else if (match_kw_ci(p->src + p->pos, L"gtr", &kwl)) {
            p->pos += kwl; node->u.if_stmt.cond = IF_COND_GTR;
        } else if (match_kw_ci(p->src + p->pos, L"geq", &kwl)) {
            p->pos += kwl; node->u.if_stmt.cond = IF_COND_GEQ;
        } else {
            cmd_ast_free(node);
            set_parse_err(p, L"The syntax of the command is incorrect.");
            return NULL;
        }
        node->u.if_stmt.rhs = read_cond_token(p, false, false);
    }

    if (!node->u.if_stmt.lhs || !*node->u.if_stmt.lhs) {
        cmd_ast_free(node);
        set_parse_err(p, L"The syntax of the command is incorrect.");
        return NULL;
    }

    skip_ws(p);
    node->u.if_stmt.then_branch = parse_stmt(p);
    if (p->has_error || !node->u.if_stmt.then_branch) {
        cmd_ast_free(node);
        if (!p->has_error) set_parse_err(p, L"The syntax of the command is incorrect.");
        return NULL;
    }

    if (node->u.if_stmt.then_branch->kind == CMD_NODE_BLOCK ||
        node->u.if_stmt.then_branch->kind == CMD_NODE_IF ||
        node->u.if_stmt.then_branch->kind == CMD_NODE_FOR) {
        skip_ws(p);
        if (match_kw_ci(p->src + p->pos, L"else", &kwl)) {
            p->pos += kwl;
            skip_ws(p);
            node->u.if_stmt.else_branch = parse_stmt(p);
            if (p->has_error || !node->u.if_stmt.else_branch) {
                cmd_ast_free(node);
                if (!p->has_error) set_parse_err(p, L"The syntax of the command is incorrect.");
                return NULL;
            }
        }
    }

    return node;
}

static cmd_node_t *parse_for(parser_t *p) {
    p->pos += 3; /* skip "for" */
    cmd_node_t *node = alloc_node(CMD_NODE_FOR);
    if (!node) return NULL;
    node->u.for_stmt.for_kind = FOR_KIND_PLAIN;

    skip_ws(p);
    if (p->src[p->pos] == L'/') {
        wchar_t sw = towupper(p->src[p->pos + 1]);
        if (sw == L'D' && (p->src[p->pos + 2] == L'\0' || is_ws(p->src[p->pos + 2]))) {
            node->u.for_stmt.for_kind = FOR_KIND_DIR;
            p->pos += 2;
        } else if (sw == L'L' && (p->src[p->pos + 2] == L'\0' || is_ws(p->src[p->pos + 2]))) {
            node->u.for_stmt.for_kind = FOR_KIND_NUM;
            p->pos += 2;
        } else if (sw == L'R' && (p->src[p->pos + 2] == L'\0' || is_ws(p->src[p->pos + 2]))) {
            node->u.for_stmt.for_kind = FOR_KIND_REC;
            p->pos += 2;
            skip_ws(p);
            if (p->src[p->pos] && p->src[p->pos] != L'%') {
                node->u.for_stmt.opts_or_root = read_cond_token(p, true, false);
            }
        } else if (sw == L'F' && (p->src[p->pos + 2] == L'\0' || is_ws(p->src[p->pos + 2]))) {
            node->u.for_stmt.for_kind = FOR_KIND_FILE;
            p->pos += 2;
            skip_ws(p);
            if (p->src[p->pos] == L'"') {
                p->pos++;
                const wchar_t *start = p->src + p->pos;
                while (p->src[p->pos] && p->src[p->pos] != L'"') p->pos++;
                size_t olen = (size_t)((p->src + p->pos) - start);
                node->u.for_stmt.opts_or_root = (wchar_t *)malloc((olen + 1) * sizeof(wchar_t));
                if (node->u.for_stmt.opts_or_root) {
                    memcpy(node->u.for_stmt.opts_or_root, start, olen * sizeof(wchar_t));
                    node->u.for_stmt.opts_or_root[olen] = L'\0';
                }
                if (p->src[p->pos] == L'"') p->pos++;
            }
        } else {
            wchar_t sw_tok[32];
            size_t si = 0;
            while (p->src[p->pos + si] && !is_ws(p->src[p->pos + si]) && si + 1 < 32) {
                sw_tok[si] = p->src[p->pos + si];
                si++;
            }
            sw_tok[si] = L'\0';
            cmd_ast_free(node);
            set_parse_err_msg(p, CMD_MSG_UNEXPECTED, L"%1 was unexpected at this time.\r\n", sw_tok);
            return NULL;
        }
    }

    skip_ws(p);
    if (p->src[p->pos] != L'%' || !iswalnum(p->src[p->pos + 1])) {
        if (!p->src[p->pos]) {
            cmd_ast_free(node);
            set_parse_err(p, L"The syntax of the command is incorrect.");
            return NULL;
        }
        wchar_t tok[32];
        size_t ti = 0;
        while (p->src[p->pos + ti] && !is_ws(p->src[p->pos + ti]) && ti + 1 < 32) {
            tok[ti] = p->src[p->pos + ti];
            ti++;
        }
        tok[ti] = L'\0';
        cmd_ast_free(node);
        set_parse_err_msg(p, CMD_MSG_UNEXPECTED, L"%1 was unexpected at this time.\r\n", tok);
        return NULL;
    }
    node->u.for_stmt.var_char = p->src[p->pos + 1];
    p->pos += 2;

    skip_ws(p);
    size_t kwl = 0;
    if (!match_kw_ci(p->src + p->pos, L"in", &kwl)) {
        cmd_ast_free(node);
        set_parse_err(p, L"The syntax of the command is incorrect.");
        return NULL;
    }
    p->pos += kwl;

    skip_ws(p);
    if (p->src[p->pos] != L'(') {
        cmd_ast_free(node);
        set_parse_err(p, L"The syntax of the command is incorrect.");
        return NULL;
    }
    p->pos++; /* skip '(' */
    const wchar_t *set_start = p->src + p->pos;
    bool in_q = false;
    while (p->src[p->pos]) {
        if (p->src[p->pos] == L'"') in_q = !in_q;
        else if (!in_q && p->src[p->pos] == L')') break;
        p->pos++;
    }
    if (p->src[p->pos] != L')') {
        cmd_ast_free(node);
        set_parse_err(p, L"The syntax of the command is incorrect.");
        return NULL;
    }
    size_t slen = (size_t)((p->src + p->pos) - set_start);
    node->u.for_stmt.set_spec = (wchar_t *)malloc((slen + 1) * sizeof(wchar_t));
    if (node->u.for_stmt.set_spec) {
        memcpy(node->u.for_stmt.set_spec, set_start, slen * sizeof(wchar_t));
        node->u.for_stmt.set_spec[slen] = L'\0';
    }
    p->pos++; /* skip ')' */

    skip_ws(p);
    if (!match_kw_ci(p->src + p->pos, L"do", &kwl)) {
        cmd_ast_free(node);
        set_parse_err(p, L"The syntax of the command is incorrect.");
        return NULL;
    }
    p->pos += kwl;

    skip_ws(p);
    node->u.for_stmt.body = parse_stmt(p);
    if (p->has_error || !node->u.for_stmt.body) {
        cmd_ast_free(node);
        if (!p->has_error) set_parse_err(p, L"The syntax of the command is incorrect.");
        return NULL;
    }
    return node;
}

static cmd_node_t *parse_simple(parser_t *p) {
    skip_ws(p);
    bool suppress_echo = false;
    while (p->src[p->pos] == L'@') {
        suppress_echo = true;
        p->pos++;
        skip_ws(p);
    }

    /* Check REM / :: */
    if ((p->src[p->pos] == L':' && p->src[p->pos + 1] == L':') ||
        (_wcsnicmp(p->src + p->pos, L"rem", 3) == 0 &&
         (p->src[p->pos + 3] == L'\0' || is_ws(p->src[p->pos + 3]) ||
          p->src[p->pos + 3] == L'.' || p->src[p->pos + 3] == L':' || p->src[p->pos + 3] == L'/'))) {
        while (p->src[p->pos]) {
            if (p->src[p->pos] == L'\r' || p->src[p->pos] == L'\n') break;
            if (p->paren_depth > 0 && p->src[p->pos] == L')') break;
            p->pos++;
        }
        return alloc_node(CMD_NODE_REM);
    }

    cmd_redir_t *redirs = NULL;
    size_t cap = 128, len = 0;
    wchar_t *raw = (wchar_t *)malloc(cap * sizeof(wchar_t));
    if (!raw) return NULL;
    bool in_q = false;

    while (p->src[p->pos]) {
        wchar_t c = p->src[p->pos];
        if (c == L'"') {
            in_q = !in_q;
            if (len + 2 >= cap) { cap *= 2; raw = (wchar_t *)realloc(raw, cap * sizeof(wchar_t)); if (!raw) { free_redirs(redirs); return NULL; } }
            raw[len++] = c;
            p->pos++;
            continue;
        }
        if (!in_q) {
            if (c == L'&' || c == L'|' || c == L'\r' || c == L'\n') break;
            if (p->paren_depth > 0 && c == L')') break;
            /* Check redirection: <, >, or [0-9][<>] */
            if (c == L'<' || c == L'>' ||
                (c >= L'0' && c <= L'9' && (p->src[p->pos + 1] == L'<' || p->src[p->pos + 1] == L'>') &&
                 (len == 0 || is_ws(raw[len - 1])))) {
                if (!try_parse_redir(p, &redirs)) {
                    /* fall through */
                } else {
                    if (p->has_error) {
                        free(raw);
                        free_redirs(redirs);
                        return NULL;
                    }
                    continue;
                }
            }
            if (c == L'^') {
                p->pos++;
                if (!p->src[p->pos]) break; /* trailing ^ */
                c = p->src[p->pos];
            }
        }
        if (len + 2 >= cap) { cap *= 2; raw = (wchar_t *)realloc(raw, cap * sizeof(wchar_t)); if (!raw) { free_redirs(redirs); return NULL; } }
        raw[len++] = c;
        p->pos++;
    }
    while (len > 0 && (raw[len - 1] == L'\r' || raw[len - 1] == L'\n')) len--;
    raw[len] = L'\0';

    if (len == 0 && !redirs) {
        free(raw);
        return NULL;
    }

    cmd_node_t *node = alloc_node(CMD_NODE_SIMPLE);
    if (!node) {
        free(raw);
        free_redirs(redirs);
        return NULL;
    }
    node->redirs = redirs;
    node->u.simple.raw_text = raw;
    node->u.simple.suppress_echo = suppress_echo;

    /* Extract cmd_word and raw_tail */
    const wchar_t *q = raw;
    while (*q && is_ws(*q)) q++;
    const wchar_t *wstart = q;
    if (*q == L'"') {
        q++;
        while (*q && *q != L'"') q++;
        if (*q == L'"') q++;
    } else {
        while (*q && !is_ws(*q)) {
            /* echo. / echo: / echo/ / echo+ / echo, / echo; / echo= / echo( */
            if (q == wstart + 4 && _wcsnicmp(wstart, L"echo", 4) == 0 &&
                (*q == L'.' || *q == L':' || *q == L'/' || *q == L'+' ||
                 *q == L',' || *q == L';' || *q == L'=' || *q == L'(')) {
                break;
            }
            q++;
        }
    }
    size_t wlen = (size_t)(q - wstart);
    node->u.simple.cmd_word = (wchar_t *)malloc((wlen + 1) * sizeof(wchar_t));
    if (node->u.simple.cmd_word) {
        memcpy(node->u.simple.cmd_word, wstart, wlen * sizeof(wchar_t));
        node->u.simple.cmd_word[wlen] = L'\0';
    }
    node->u.simple.raw_tail = _wcsdup(q);
    return node;
}

static cmd_node_t *parse_stmt(parser_t *p) {
    skip_ws(p);
    while (p->src[p->pos] == L'@') {
        /* Handle @ before block/if/for or simple */
        const wchar_t *after = p->src + p->pos + 1;
        while (*after && is_ws(*after)) after++;
        if (*after == L'(' || _wcsnicmp(after, L"if", 2) == 0 || _wcsnicmp(after, L"for", 3) == 0) {
            p->pos = (size_t)(after - p->src);
            continue;
        }
        break;
    }

    if (p->src[p->pos] == L'(') {
        p->pos++;
        p->paren_depth++;
        cmd_node_t *body = parse_seq(p);
        skip_ws(p);
        if (p->src[p->pos] != L')') {
            cmd_ast_free(body);
            p->silent_err = (p->src[p->pos] == L'\0');
            set_parse_err(p, L"The syntax of the command is incorrect.");
            return NULL;
        }
        p->pos++;
        p->paren_depth--;
        cmd_node_t *blk = alloc_node(CMD_NODE_BLOCK);
        if (!blk) { cmd_ast_free(body); return NULL; }
        blk->u.block.body = body;
        parse_trailing_redirs(p, &blk->redirs);
        return blk;
    }

    size_t kwl = 0;
    if (match_kw_ci(p->src + p->pos, L"if", &kwl)) {
        return parse_if(p);
    }
    if (match_kw_ci(p->src + p->pos, L"for", &kwl)) {
        return parse_for(p);
    }
    return parse_simple(p);
}

static cmd_node_t *parse_pipe(parser_t *p) {
    cmd_node_t *left = parse_stmt(p);
    if (p->has_error || !left) return left;

    for (;;) {
        skip_hws(p);
        if (p->src[p->pos] == L'|' && p->src[p->pos + 1] != L'|') {
            p->pos++;
            cmd_node_t *right = parse_stmt(p);
            if (p->has_error || !right) {
                cmd_ast_free(left);
                if (!p->has_error) set_parse_err(p, L"The syntax of the command is incorrect.");
                return NULL;
            }
            cmd_node_t *bin = alloc_node(CMD_NODE_PIPE);
            if (!bin) { cmd_ast_free(left); cmd_ast_free(right); return NULL; }
            bin->u.bin.left = left;
            bin->u.bin.right = right;
            left = bin;
        } else {
            break;
        }
    }
    return left;
}

static cmd_node_t *parse_and_or(parser_t *p) {
    cmd_node_t *left = parse_pipe(p);
    if (p->has_error || !left) return left;

    for (;;) {
        skip_hws(p);
        cmd_node_kind_t k;
        if (p->src[p->pos] == L'&' && p->src[p->pos + 1] == L'&') {
            k = CMD_NODE_AND;
            p->pos += 2;
        } else if (p->src[p->pos] == L'|' && p->src[p->pos + 1] == L'|') {
            k = CMD_NODE_OR;
            p->pos += 2;
        } else {
            break;
        }
        cmd_node_t *right = parse_pipe(p);
        if (p->has_error || !right) {
            cmd_ast_free(left);
            if (!p->has_error) set_parse_err(p, L"The syntax of the command is incorrect.");
            return NULL;
        }
        cmd_node_t *bin = alloc_node(k);
        if (!bin) { cmd_ast_free(left); cmd_ast_free(right); return NULL; }
        bin->u.bin.left = left;
        bin->u.bin.right = right;
        left = bin;
    }
    return left;
}

static cmd_node_t *parse_seq(parser_t *p) {
    cmd_node_t *left = parse_and_or(p);
    if (p->has_error || !left) return left;

    for (;;) {
        skip_hws(p);
        bool is_amp = (p->src[p->pos] == L'&' && p->src[p->pos + 1] != L'&');
        bool is_nl = (p->paren_depth > 0 && (p->src[p->pos] == L'\n' || p->src[p->pos] == L'\r'));
        if (is_amp || is_nl) {
            if (is_amp) p->pos++;
            skip_ws(p);
            if (!p->src[p->pos] || (p->paren_depth > 0 && p->src[p->pos] == L')')) {
                break; /* trailing & or newline before ) */
            }
            cmd_node_t *right = parse_and_or(p);
            if (p->has_error || !right) {
                cmd_ast_free(left);
                return NULL;
            }
            cmd_node_t *bin = alloc_node(CMD_NODE_SEQ);
            if (!bin) { cmd_ast_free(left); cmd_ast_free(right); return NULL; }
            bin->u.bin.left = left;
            bin->u.bin.right = right;
            left = bin;
        } else {
            break;
        }
    }
    return left;
}

cmd_node_t *cmd_ast_parse_ex(const wchar_t *line, wchar_t *err_buf, size_t err_cap, bool *out_silent) {
    if (err_buf && err_cap > 0) err_buf[0] = L'\0';
    if (out_silent) *out_silent = false;
    if (!line) return NULL;
    parser_t p = { line, 0, 0, err_buf, err_cap, false, false };
    skip_ws(&p);
    if (!p.src[p.pos]) return NULL;

    cmd_node_t *root = parse_seq(&p);
    if (p.has_error) {
        if (out_silent) *out_silent = p.silent_err;
        cmd_ast_free(root);
        return NULL;
    }
    skip_ws(&p);
    if (p.src[p.pos] != L'\0') {
        cmd_ast_free(root);
        set_parse_err(&p, L"The syntax of the command is incorrect.");
        return NULL;
    }
    return root;
}

cmd_node_t *cmd_ast_parse(const wchar_t *line, wchar_t *err_buf, size_t err_cap) {
    return cmd_ast_parse_ex(line, err_buf, err_cap, NULL);
}
