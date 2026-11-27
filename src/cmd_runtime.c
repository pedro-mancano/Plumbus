#include "cmd_runtime.h"
#include "cmd_expand.h"
#include "cmd_ast.h"
#include "resolver.h"
#include "config.h"
#include "tui_config.h"
#include "console.h"
#include "git_native.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wctype.h>

typedef struct {
    wchar_t **lines;
    size_t count;
} batch_lines_t;

typedef struct {
    const batch_lines_t *bl;
} batch_extra_t;

static int eval_node(shell_ctx_t *ctx, const cmd_node_t *node, const cmd_io_t *io);
static int exec_line_internal(shell_ctx_t *ctx, const wchar_t *line, const cmd_io_t *io,
                              bool is_batch, bool is_top_interactive);

static wchar_t *expand_dynamic_tokens(const shell_ctx_t *ctx, const wchar_t *s) {
    if (!s) return _wcsdup(L"");
    wchar_t *f = cmd_expand_for_vars(ctx, s);
    wchar_t *d = cmd_expand_delayed(ctx, f ? f : s);
    free(f);
    return d ? d : _wcsdup(s);
}

static bool cwd_has_config_command(void) {
    const wchar_t *exts[] = { L"config.exe", L"config.bat", L"config.cmd", L"config.com" };
    for (int i = 0; i < 4; i++) {
        DWORD a = GetFileAttributesW(exts[i]);
        if (a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY)) return true;
    }
    return false;
}

static bool try_exec_plumbus_builtin(shell_ctx_t *ctx, const wchar_t *p, const cmd_io_t *io) {
    plumbus_config_t *cfg = (plumbus_config_t *)ctx->config_ptr;
    if (!cfg) return false;

    wchar_t trimmed[512];
    wcsncpy(trimmed, p, 511);
    trimmed[511] = L'\0';
    cmd_trim_trailing_ws(trimmed);

    bool is_plumbus_cfg =
        (_wcsnicmp(trimmed, L"plumbus", 7) == 0 && (trimmed[7] == L' ' || trimmed[7] == L'\t') &&
         _wcsicmp(cmd_skip_ws(trimmed + 7), L"config") == 0) ||
        (_wcsnicmp(trimmed, L"plumbus.exe", 11) == 0 && (trimmed[11] == L' ' || trimmed[11] == L'\t') &&
         _wcsicmp(cmd_skip_ws(trimmed + 11), L"config") == 0);
    bool is_bare_cfg = (_wcsicmp(trimmed, L"config") == 0 && !cwd_has_config_command());
    if (is_plumbus_cfg || is_bare_cfg) {
        wchar_t cwd[MAX_PATH * 4];
        if (GetCurrentDirectoryW((DWORD)(sizeof(cwd) / sizeof(cwd[0])), cwd) == 0) wcscpy(cwd, L"~");
        git_status_t git_st = { 0 };
        if (cfg->git_status) git_native_inspect(cwd, &git_st);
        console_state_t con;
        console_init(&con);
        console_enter_raw(&con);
        tui_cfg_run(cfg, cwd, ctx->last_exit_code, &git_st);
        console_restore(&con);
        ctx->last_exit_code = 0;
        return true;
    }

    if (_wcsnicmp(p, L"alias", 5) == 0 && (p[5] == L'\0' || p[5] == L' ' || p[5] == L'\t')) {
        const wchar_t *args = cmd_skip_ws(p + 5);
        if (!*args) {
            for (size_t i = 0; i < cfg->alias_count; i++) {
                cmd_io_printf(io->h_out, L"%ls=%ls\r\n", cfg->aliases[i].name, cfg->aliases[i].value);
            }
            ctx->last_exit_code = 0;
            return true;
        }
        const wchar_t *eq = wcschr(args, L'=');
        if (eq) {
            size_t nlen = (size_t)(eq - args);
            wchar_t name[256];
            if (nlen > 0 && nlen < 256) {
                memcpy(name, args, nlen * sizeof(wchar_t));
                name[nlen] = L'\0';
                cmd_trim_trailing_ws(name);
                wchar_t *val = _wcsdup(cmd_skip_ws(eq + 1));
                if (val) {
                    cmd_trim_trailing_ws(val);
                    cmd_strip_outer_quotes(val);
                    config_set_alias(cfg, name, val);
                    free(val);
                }
            }
            ctx->last_exit_code = 0;
            return true;
        }
        const wchar_t *found = config_find_alias(cfg, args);
        if (found) {
            cmd_io_printf(io->h_out, L"%ls=%ls\r\n", args, found);
            ctx->last_exit_code = 0;
        } else {
            ctx->last_exit_code = 1;
        }
        return true;
    }

    if (_wcsnicmp(p, L"unalias", 7) == 0 && (p[7] == L' ' || p[7] == L'\t')) {
        wchar_t name[256];
        wcsncpy(name, cmd_skip_ws(p + 7), 255);
        name[255] = L'\0';
        cmd_trim_trailing_ws(name);
        ctx->last_exit_code = config_remove_alias(cfg, name) ? 0 : 1;
        return true;
    }

    if (_wcsnicmp(p, L"theme", 5) == 0 && (p[5] == L'\0' || p[5] == L' ' || p[5] == L'\t')) {
        wchar_t arg[128];
        wcsncpy(arg, cmd_skip_ws(p + 5), 127);
        arg[127] = L'\0';
        cmd_trim_trailing_ws(arg);
        if (!*arg) {
            cmd_io_printf(io->h_out, L"%ls\r\n", config_theme_name(cfg->theme));
        } else if (!config_theme_from_name(arg, &cfg->theme)) {
            cmd_io_printf(io->h_err, L"Unknown theme: %ls (available: robbyrussell, agnoster, omb-box, pure, dracula, minimal)\r\n", arg);
            ctx->last_exit_code = 1;
            return true;
        }
        ctx->last_exit_code = 0;
        return true;
    }

    return false;
}

typedef struct {
    HANDLE owned[16];
    int owned_count;
} redir_state_t;

static bool apply_redirs(const shell_ctx_t *ctx, const cmd_redir_t *redirs,
                         const cmd_io_t *base_io, cmd_io_t *out_io, redir_state_t *rs) {
    *out_io = *base_io;
    rs->owned_count = 0;
    SECURITY_ATTRIBUTES sa = { sizeof(sa), NULL, TRUE };

    for (const cmd_redir_t *r = redirs; r; r = r->next) {
        if (r->kind == CMD_REDIR_DUP) {
            HANDLE src_h = (r->dst_fd == 0) ? out_io->h_in : (r->dst_fd == 2 ? out_io->h_err : out_io->h_out);
            if (r->src_fd == 0) out_io->h_in = src_h;
            else if (r->src_fd == 2) out_io->h_err = src_h;
            else out_io->h_out = src_h;
            continue;
        }

        wchar_t *tgt = expand_dynamic_tokens(ctx, r->target);
        if (!tgt) return false;

        HANDLE h = INVALID_HANDLE_VALUE;
        if (r->kind == CMD_REDIR_IN) {
            h = CreateFileW(tgt, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                            &sa, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        } else if (r->kind == CMD_REDIR_OUT_TRUNC) {
            h = CreateFileW(tgt, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                            &sa, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        } else {
            h = CreateFileW(tgt, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                            &sa, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
            if (h != INVALID_HANDLE_VALUE) {
                LARGE_INTEGER zero = { 0 };
                SetFilePointerEx(h, zero, NULL, FILE_END);
            }
        }
        free(tgt);

        if (h == INVALID_HANDLE_VALUE) {
            cmd_io_write_sz(out_io->h_err, L"The system cannot find the file specified.\r\n");
            return false;
        }
        if (rs->owned_count < 16) rs->owned[rs->owned_count++] = h;
        if (r->src_fd == 0) out_io->h_in = h;
        else if (r->src_fd == 2) out_io->h_err = h;
        else out_io->h_out = h;
    }
    return true;
}

static void close_redir_handles(redir_state_t *rs) {
    for (int i = 0; i < rs->owned_count; i++) {
        CloseHandle(rs->owned[i]);
    }
    rs->owned_count = 0;
}

static HANDLE ensure_inheritable(HANDLE h, HANDLE *dup_out) {
    *dup_out = NULL;
    if (!h || h == INVALID_HANDLE_VALUE) return h;
    DWORD flags = 0;
    if (GetHandleInformation(h, &flags) && (flags & HANDLE_FLAG_INHERIT)) {
        return h;
    }
    HANDLE dup = NULL;
    if (DuplicateHandle(GetCurrentProcess(), h, GetCurrentProcess(), &dup, 0, TRUE, DUPLICATE_SAME_ACCESS)) {
        *dup_out = dup;
        return dup;
    }
    return h;
}

static int spawn_external_exe(shell_ctx_t *ctx, const wchar_t *exe_path,
                              const wchar_t *raw_tail, const cmd_io_t *io) {
    size_t elen = wcslen(exe_path);
    size_t tlen = raw_tail ? wcslen(raw_tail) : 0;
    wchar_t *cmdline = (wchar_t *)malloc((elen + 2 + tlen + 1) * sizeof(wchar_t));
    if (!cmdline) return 1;
    cmdline[0] = L'"';
    memcpy(cmdline + 1, exe_path, elen * sizeof(wchar_t));
    cmdline[1 + elen] = L'"';
    if (tlen > 0) memcpy(cmdline + 2 + elen, raw_tail, tlen * sizeof(wchar_t));
    cmdline[2 + elen + tlen] = L'\0';

    HANDLE dup_in = NULL, dup_out = NULL, dup_err = NULL;
    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof(si));
    ZeroMemory(&pi, sizeof(pi));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = ensure_inheritable(io->h_in, &dup_in);
    si.hStdOutput = ensure_inheritable(io->h_out, &dup_out);
    si.hStdError = ensure_inheritable(io->h_err, &dup_err);

    SetConsoleCtrlHandler(NULL, TRUE);
    BOOL ok = CreateProcessW(exe_path, cmdline, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi);
    free(cmdline);
    if (dup_in) CloseHandle(dup_in);
    if (dup_out) CloseHandle(dup_out);
    if (dup_err) CloseHandle(dup_err);

    if (!ok) {
        SetConsoleCtrlHandler(NULL, FALSE);
        ctx->last_exit_code = (int)GetLastError();
        return ctx->last_exit_code;
    }

    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD ec = 0;
    GetExitCodeProcess(pi.hProcess, &ec);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    SetConsoleCtrlHandler(NULL, FALSE);

    ctx->last_exit_code = (int)ec;
    return ctx->last_exit_code;
}

typedef struct {
    shell_ctx_t *ctx;
    const cmd_node_t *node;
    cmd_io_t io;
    int rc;
} pipe_worker_t;

static DWORD WINAPI pipe_lhs_thread(LPVOID param) {
    pipe_worker_t *w = (pipe_worker_t *)param;
    w->rc = eval_node(w->ctx, w->node, &w->io);
    CloseHandle(w->io.h_out);
    return 0;
}

static int eval_pipe(shell_ctx_t *ctx, const cmd_node_t *left, const cmd_node_t *right, const cmd_io_t *io) {
    SECURITY_ATTRIBUTES sa = { sizeof(sa), NULL, TRUE };
    HANDLE pr = NULL, pw = NULL;
    if (!CreatePipe(&pr, &pw, &sa, 0)) return 1;

    shell_ctx_t lhs_ctx = *ctx;
    lhs_ctx.dir_stack = NULL;
    lhs_ctx.dir_stack_len = 0;
    lhs_ctx.dir_stack_cap = 0;
    lhs_ctx.local_stack = NULL;
    lhs_ctx.local_stack_len = 0;
    lhs_ctx.local_stack_cap = 0;

    pipe_worker_t worker;
    worker.ctx = &lhs_ctx;
    worker.node = left;
    worker.io.h_in = io->h_in;
    worker.io.h_out = pw;
    worker.io.h_err = io->h_err;
    worker.rc = 0;

    HANDLE th = CreateThread(NULL, 0, pipe_lhs_thread, &worker, 0, NULL);
    if (!th) {
        CloseHandle(pr);
        CloseHandle(pw);
        return 1;
    }

    cmd_io_t rhs_io = { pr, io->h_out, io->h_err };
    int rc = eval_node(ctx, right, &rhs_io);

    WaitForSingleObject(th, INFINITE);
    CloseHandle(th);
    CloseHandle(pr);
    shell_ctx_free(&lhs_ctx);
    ctx->last_exit_code = rc;
    return rc;
}

static bool parse_batch_args_preserve_quotes(const wchar_t *tail, int *out_argc, wchar_t ***out_argv) {
    *out_argc = 0;
    *out_argv = NULL;
    if (!tail) return true;

    int cap = 16, argc = 0;
    wchar_t **argv = (wchar_t **)malloc((size_t)cap * sizeof(wchar_t *));
    if (!argv) return false;

    const wchar_t *p = tail;
    for (;;) {
        while (*p == L' ' || *p == L'\t' || *p == L',' || *p == L';') p++;
        if (!*p) break;
        const wchar_t *start = p;
        bool in_q = false;
        while (*p) {
            if (*p == L'"') in_q = !in_q;
            else if (!in_q && (*p == L' ' || *p == L'\t' || *p == L',' || *p == L';')) break;
            p++;
        }
        size_t len = (size_t)(p - start);
        wchar_t *tok = (wchar_t *)malloc((len + 1) * sizeof(wchar_t));
        if (!tok) { cmd_free_args(argc, argv); return false; }
        memcpy(tok, start, len * sizeof(wchar_t));
        tok[len] = L'\0';
        if (argc == cap) {
            cap *= 2;
            wchar_t **na = (wchar_t **)realloc(argv, (size_t)cap * sizeof(wchar_t *));
            if (!na) { free(tok); cmd_free_args(argc, argv); return false; }
            argv = na;
        }
        argv[argc++] = tok;
    }
    *out_argc = argc;
    *out_argv = argv;
    return true;
}

static int run_batch_lines(shell_ctx_t *ctx, const batch_lines_t *bl, size_t start_idx,
                           const wchar_t *script_path, const wchar_t *raw_tail, const cmd_io_t *io) {
    int argc = 0;
    wchar_t **argv = NULL;
    parse_batch_args_preserve_quotes(raw_tail, &argc, &argv);

    /* Build argv with argv[0] = script_path */
    int full_argc = argc + 1;
    wchar_t **full_argv = (wchar_t **)malloc((size_t)full_argc * sizeof(wchar_t *));
    if (!full_argv) {
        cmd_free_args(argc, argv);
        return 1;
    }
    full_argv[0] = _wcsdup(script_path);
    for (int i = 0; i < argc; i++) full_argv[i + 1] = argv[i];
    free(argv);

    cmd_batch_frame_t frame;
    memset(&frame, 0, sizeof(frame));
    frame.script_path = script_path;
    frame.argc = full_argc;
    frame.argv = full_argv;
    frame.raw_tail = raw_tail ? cmd_skip_ws(raw_tail) : L"";
    frame.local_stack_base = ctx->local_stack_len;
    frame.parent = (cmd_batch_frame_t *)ctx->batch_frame;
    ctx->batch_frame = &frame;

    size_t idx = start_idx;
    while (idx < bl->count && !ctx->should_exit && !frame.exit_batch) {
        /* Collect multi-line (...) or ^ continuation block */
        size_t cap = 256, len = 0;
        wchar_t *stmt = (wchar_t *)malloc(cap * sizeof(wchar_t));
        if (!stmt) break;
        stmt[0] = L'\0';

        int paren_depth = 0;
        bool in_q = false;
        while (idx < bl->count) {
            const wchar_t *ln = bl->lines[idx];
            const wchar_t *trimmed = cmd_skip_ws(ln);
            /* Skip label lines */
            if (paren_depth == 0 && len == 0 && trimmed[0] == L':' && trimmed[1] != L':') {
                idx++;
                break;
            }
            size_t llen = wcslen(ln);
            bool has_cont = (llen > 0 && ln[llen - 1] == L'^');
            size_t add_len = has_cont ? (llen - 1) : llen;
            if (len + add_len + 3 >= cap) {
                cap = (len + add_len + 64) * 2;
                wchar_t *ns = (wchar_t *)realloc(stmt, cap * sizeof(wchar_t));
                if (!ns) break;
                stmt = ns;
            }
            if (len > 0) stmt[len++] = L' ';
            memcpy(stmt + len, ln, add_len * sizeof(wchar_t));
            len += add_len;
            stmt[len] = L'\0';

            for (size_t k = 0; k < add_len; k++) {
                wchar_t c = ln[k];
                if (c == L'"') in_q = !in_q;
                else if (!in_q) {
                    if (c == L'^' && k + 1 < add_len) { k++; continue; }
                    if (c == L'(') paren_depth++;
                    else if (c == L')' && paren_depth > 0) paren_depth--;
                }
            }
            idx++;
            if (!has_cont && paren_depth == 0) break;
        }

        if (stmt[0]) {
            exec_line_internal(ctx, stmt, io, true, false);
        }
        free(stmt);

        if (frame.goto_pending) {
            frame.goto_pending = false;
            if (_wcsicmp(frame.goto_label, L"EOF") == 0) {
                break;
            }
            bool found = false;
            for (size_t li = 0; li < bl->count; li++) {
                const wchar_t *p = cmd_skip_ws(bl->lines[li]);
                if (p[0] == L':' && p[1] != L':') {
                    p++;
                    wchar_t lbl[256];
                    size_t lbi = 0;
                    while (*p && !iswspace(*p) && lbi + 1 < 256) lbl[lbi++] = *p++;
                    lbl[lbi] = L'\0';
                    if (_wcsicmp(lbl, frame.goto_label) == 0) {
                        idx = li + 1;
                        found = true;
                        break;
                    }
                }
            }
            if (!found) {
                cmd_io_printf(io->h_err, L"The system cannot find the batch label specified - %ls\r\n", frame.goto_label);
                ctx->last_exit_code = 1;
                break;
            }
        }
    }

    builtins_pop_locals_to(ctx, frame.local_stack_base);
    ctx->batch_frame = frame.parent;
    cmd_free_args(full_argc, full_argv);
    if (frame.exit_batch) ctx->last_exit_code = frame.exit_code;
    return ctx->last_exit_code;
}

static bool load_batch_file(const wchar_t *path, batch_lines_t *out) {
    out->lines = NULL;
    out->count = 0;

    HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                           NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER fsz;
    if (!GetFileSizeEx(h, &fsz) || fsz.QuadPart > 16 * 1024 * 1024) {
        CloseHandle(h);
        return false;
    }
    DWORD sz = (DWORD)fsz.QuadPart;
    char *raw = (char *)malloc((size_t)sz + 1);
    if (!raw) { CloseHandle(h); return false; }
    DWORD rd = 0;
    if (sz > 0 && !ReadFile(h, raw, sz, &rd, NULL)) {
        free(raw); CloseHandle(h); return false;
    }
    raw[rd] = '\0';
    CloseHandle(h);

    const char *src = raw;
    DWORD len = rd;
    if (len >= 3 && (unsigned char)src[0] == 0xEF && (unsigned char)src[1] == 0xBB && (unsigned char)src[2] == 0xBF) {
        src += 3;
        len -= 3;
    }

    int wn = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, src, (int)len, NULL, 0);
    UINT cp = CP_UTF8;
    if (wn <= 0 && len > 0) {
        cp = CP_ACP;
        wn = MultiByteToWideChar(cp, 0, src, (int)len, NULL, 0);
    }
    wchar_t *wtext = (wchar_t *)malloc(((size_t)wn + 1) * sizeof(wchar_t));
    if (!wtext) { free(raw); return false; }
    if (wn > 0) MultiByteToWideChar(cp, 0, src, (int)len, wtext, wn);
    wtext[wn] = L'\0';
    free(raw);

    size_t cap = 64;
    out->lines = (wchar_t **)malloc(cap * sizeof(wchar_t *));
    if (!out->lines) { free(wtext); return false; }

    const wchar_t *p = wtext;
    while (*p) {
        const wchar_t *eol = p;
        while (*eol && *eol != L'\n' && *eol != L'\r') eol++;
        size_t llen = (size_t)(eol - p);
        wchar_t *ln = (wchar_t *)malloc((llen + 1) * sizeof(wchar_t));
        if (ln) {
            memcpy(ln, p, llen * sizeof(wchar_t));
            ln[llen] = L'\0';
            if (out->count == cap) {
                cap *= 2;
                wchar_t **nl = (wchar_t **)realloc(out->lines, cap * sizeof(wchar_t *));
                if (!nl) { free(ln); break; }
                out->lines = nl;
            }
            out->lines[out->count++] = ln;
        }
        if (*eol == L'\r' && eol[1] == L'\n') p = eol + 2;
        else if (*eol) p = eol + 1;
        else break;
    }
    free(wtext);
    return true;
}

static void free_batch_lines(batch_lines_t *bl) {
    for (size_t i = 0; i < bl->count; i++) free(bl->lines[i]);
    free(bl->lines);
    bl->lines = NULL;
    bl->count = 0;
}

/* Global map is unnecessary if we attach lines pointer? Wait: CALL :label needs current script lines.
 * Let's store a thread-local / simple stack pointer for active batch_lines_t! */
static const batch_lines_t *g_active_bl = NULL;

int cmd_runtime_exec_batch_file(shell_ctx_t *ctx, const wchar_t *script_path,
                                const wchar_t *raw_tail, const cmd_io_t *io) {
    batch_lines_t bl;
    if (!load_batch_file(script_path, &bl)) {
        cmd_io_printf(io->h_err, L"The system cannot find the file %ls.\r\n", script_path);
        ctx->last_exit_code = 1;
        return 1;
    }
    const batch_lines_t *prev_bl = g_active_bl;
    g_active_bl = &bl;
    int rc = run_batch_lines(ctx, &bl, 0, script_path, raw_tail, io);
    g_active_bl = prev_bl;
    free_batch_lines(&bl);
    return rc;
}

static int exec_call(shell_ctx_t *ctx, const wchar_t *tail, const cmd_io_t *io) {
    const wchar_t *p = cmd_skip_ws(tail);
    if (!*p) return 0;

    if (p[0] == L':') {
        const cmd_batch_frame_t *bf = (const cmd_batch_frame_t *)ctx->batch_frame;
        if (!bf || !g_active_bl) return 1;
        p++;
        wchar_t lbl[256];
        size_t lbi = 0;
        while (*p && !iswspace(*p) && lbi + 1 < 256) lbl[lbi++] = *p++;
        lbl[lbi] = L'\0';
        const wchar_t *args_tail = cmd_skip_ws(p);

        for (size_t li = 0; li < g_active_bl->count; li++) {
            const wchar_t *lp = cmd_skip_ws(g_active_bl->lines[li]);
            if (lp[0] == L':' && lp[1] != L':') {
                lp++;
                wchar_t cur[256];
                size_t ci = 0;
                while (*lp && !iswspace(*lp) && ci + 1 < 256) cur[ci++] = *lp++;
                cur[ci] = L'\0';
                if (_wcsicmp(cur, lbl) == 0) {
                    return run_batch_lines(ctx, g_active_bl, li + 1, bf->script_path, args_tail, io);
                }
            }
        }
        cmd_io_printf(io->h_err, L"The system cannot find the batch label specified - %ls\r\n", lbl);
        ctx->last_exit_code = 1;
        return 1;
    }

    /* Extract first token to see if it's a .bat/.cmd */
    wchar_t *exp = cmd_expand_percent(ctx, p, ctx->batch_frame != NULL);
    int rc = exec_line_internal(ctx, exp ? exp : p, io, ctx->batch_frame != NULL, false);
    free(exp);
    return rc;
}

static int eval_simple(shell_ctx_t *ctx, const cmd_node_t *node, const cmd_io_t *io) {
    cmd_io_t local_io;
    redir_state_t rs;
    if (!apply_redirs(ctx, node->redirs, io, &local_io, &rs)) {
        close_redir_handles(&rs);
        ctx->last_exit_code = 1;
        return 1;
    }

    wchar_t *exp_cmd = expand_dynamic_tokens(ctx, node->u.simple.cmd_word);
    wchar_t *exp_tail = expand_dynamic_tokens(ctx, node->u.simple.raw_tail);
    const wchar_t *cmd = exp_cmd ? exp_cmd : L"";
    const wchar_t *tail = exp_tail ? exp_tail : L"";

    /* Strip outer quotes from cmd_word for matching */
    wchar_t clean_cmd[MAX_PATH * 4];
    wcsncpy(clean_cmd, cmd, (sizeof(clean_cmd) / sizeof(clean_cmd[0])) - 1);
    clean_cmd[(sizeof(clean_cmd) / sizeof(clean_cmd[0])) - 1] = L'\0';
    cmd_strip_outer_quotes(clean_cmd);

    int rc = 0;
    if (_wcsicmp(clean_cmd, L"exit") == 0) {
        const wchar_t *ap = cmd_skip_ws(tail);
        bool b_flag = false;
        if (_wcsnicmp(ap, L"/b", 2) == 0 && (ap[2] == L'\0' || iswspace(ap[2]))) {
            b_flag = true;
            ap = cmd_skip_ws(ap + 2);
        }
        int code = *ap ? _wtoi(ap) : ctx->last_exit_code;
        if (b_flag && ctx->batch_frame) {
            cmd_batch_frame_t *bf = (cmd_batch_frame_t *)ctx->batch_frame;
            bf->exit_batch = true;
            bf->exit_code = code;
        } else {
            ctx->should_exit = true;
            ctx->last_exit_code = code;
        }
        rc = code;
        goto done;
    }

    if (_wcsicmp(clean_cmd, L"goto") == 0) {
        cmd_batch_frame_t *bf = (cmd_batch_frame_t *)ctx->batch_frame;
        if (bf) {
            const wchar_t *ap = cmd_skip_ws(tail);
            if (*ap == L':') ap++;
            size_t li = 0;
            while (*ap && !iswspace(*ap) && li + 1 < 256) bf->goto_label[li++] = *ap++;
            bf->goto_label[li] = L'\0';
            bf->goto_pending = true;
        }
        rc = 0;
        goto done;
    }

    if (_wcsicmp(clean_cmd, L"shift") == 0) {
        cmd_batch_frame_t *bf = (cmd_batch_frame_t *)ctx->batch_frame;
        if (bf && bf->shift_offset + 1 < bf->argc) {
            bf->shift_offset++;
        }
        rc = 0;
        goto done;
    }

    if (_wcsicmp(clean_cmd, L"call") == 0) {
        rc = exec_call(ctx, tail, &local_io);
        goto done;
    }

    bool handled = false;
    rc = builtins_dispatch(ctx, clean_cmd, tail, &local_io, &handled);
    if (handled) goto done;

    /* External executable or batch file */
    wchar_t full_cmd_buf[8192];
    swprintf(full_cmd_buf, 8192, L"%ls%ls", cmd, tail);
    cmd_resolution_t res;
    resolver_classify_and_find(full_cmd_buf, &res);

    if (res.kind == CMD_KIND_BATCH_SCRIPT) {
        cmd_batch_frame_t *caller = (cmd_batch_frame_t *)ctx->batch_frame;
        rc = cmd_runtime_exec_batch_file(ctx, res.resolved_path, tail, &local_io);
        /* Without CALL, transferring to another batch terminates caller */
        if (caller) {
            caller->exit_batch = true;
            caller->exit_code = rc;
        }
    } else if (res.kind == CMD_KIND_EXTERNAL_EXE) {
        rc = spawn_external_exe(ctx, res.resolved_path, tail, &local_io);
    } else {
        cmd_io_printf(local_io.h_err,
                      L"'%ls' is not recognized as an internal or external command,\r\noperable program or batch file.\r\n",
                      clean_cmd);
        ctx->last_exit_code = 9009;
        rc = 9009;
    }

done:
    free(exp_cmd);
    free(exp_tail);
    close_redir_handles(&rs);
    return rc;
}

static bool try_parse_i64(const wchar_t *s, long long *out) {
    if (!s || !*s) return false;
    wchar_t *end = NULL;
    long long v = wcstoll(s, &end, 0);
    if (end == s || *end != L'\0') return false;
    *out = v;
    return true;
}

static int eval_if(shell_ctx_t *ctx, const cmd_node_t *node, const cmd_io_t *io) {
    wchar_t *lhs = expand_dynamic_tokens(ctx, node->u.if_stmt.lhs);
    wchar_t *rhs = node->u.if_stmt.rhs ? expand_dynamic_tokens(ctx, node->u.if_stmt.rhs) : NULL;
    bool result = false;

    switch (node->u.if_stmt.cond) {
        case IF_COND_EXIST: {
            WIN32_FIND_DATAW fd;
            HANDLE hf = FindFirstFileW(lhs, &fd);
            if (hf != INVALID_HANDLE_VALUE) {
                FindClose(hf);
                result = true;
            }
            break;
        }
        case IF_COND_DEFINED: {
            wchar_t *val = NULL;
            if (cmd_lookup_var(ctx, lhs, &val)) {
                result = true;
                free(val);
            }
            break;
        }
        case IF_COND_ERRORLEVEL: {
            int n = _wtoi(lhs);
            result = (ctx->last_exit_code >= n);
            break;
        }
        case IF_COND_CMDEXTVERSION: {
            int n = _wtoi(lhs);
            result = (ctx->extensions_enabled && 2 >= n);
            break;
        }
        case IF_COND_EQ_STR:
            result = node->u.if_stmt.case_insensitive
                ? (_wcsicmp(lhs, rhs ? rhs : L"") == 0)
                : (wcscmp(lhs, rhs ? rhs : L"") == 0);
            break;
        default: {
            long long a = 0, b = 0;
            int cmp = 0;
            if (try_parse_i64(lhs, &a) && try_parse_i64(rhs, &b)) {
                cmp = (a < b) ? -1 : (a > b ? 1 : 0);
            } else {
                cmp = node->u.if_stmt.case_insensitive
                    ? _wcsicmp(lhs, rhs ? rhs : L"")
                    : wcscmp(lhs, rhs ? rhs : L"");
            }
            switch (node->u.if_stmt.cond) {
                case IF_COND_EQU: result = (cmp == 0); break;
                case IF_COND_NEQ: result = (cmp != 0); break;
                case IF_COND_LSS: result = (cmp < 0); break;
                case IF_COND_LEQ: result = (cmp <= 0); break;
                case IF_COND_GTR: result = (cmp > 0); break;
                case IF_COND_GEQ: result = (cmp >= 0); break;
                default: break;
            }
            break;
        }
    }

    free(lhs);
    free(rhs);
    if (node->u.if_stmt.negated) result = !result;

    if (result) {
        return eval_node(ctx, node->u.if_stmt.then_branch, io);
    } else if (node->u.if_stmt.else_branch) {
        return eval_node(ctx, node->u.if_stmt.else_branch, io);
    }
    return ctx->last_exit_code;
}

static int run_for_body_with(shell_ctx_t *ctx, wchar_t var_char, const wchar_t *val,
                             const cmd_node_t *body, const cmd_io_t *io) {
    cmd_for_var_t fv;
    fv.letter = var_char;
    fv.value = val;
    fv.prev = (cmd_for_var_t *)ctx->for_vars;
    ctx->for_vars = &fv;
    int rc = eval_node(ctx, body, io);
    ctx->for_vars = fv.prev;
    return rc;
}

static void split_for_set(const wchar_t *set_spec, wchar_t ***out_items, size_t *out_n) {
    size_t cap = 16, n = 0;
    wchar_t **items = (wchar_t **)malloc(cap * sizeof(wchar_t *));
    const wchar_t *p = set_spec;
    while (*p) {
        while (*p == L' ' || *p == L'\t' || *p == L',' || *p == L';') p++;
        if (!*p) break;
        size_t tcap = 64, tlen = 0;
        wchar_t *tok = (wchar_t *)malloc(tcap * sizeof(wchar_t));
        bool in_q = false;
        while (*p) {
            if (*p == L'"') { in_q = !in_q; p++; continue; }
            if (!in_q && (*p == L' ' || *p == L'\t' || *p == L',' || *p == L';')) break;
            if (tlen + 2 >= tcap) { tcap *= 2; tok = (wchar_t *)realloc(tok, tcap * sizeof(wchar_t)); }
            tok[tlen++] = *p++;
        }
        tok[tlen] = L'\0';
        if (n == cap) { cap *= 2; items = (wchar_t **)realloc(items, cap * sizeof(wchar_t *)); }
        items[n++] = tok;
    }
    *out_items = items;
    *out_n = n;
}

static void for_walk_rec(shell_ctx_t *ctx, const wchar_t *dir, wchar_t var_char,
                         wchar_t **pats, size_t npats, const cmd_node_t *body, const cmd_io_t *io) {
    for (size_t i = 0; i < npats; i++) {
        wchar_t full[MAX_PATH * 4];
        swprintf(full, sizeof(full) / sizeof(full[0]), L"%ls\\%ls", dir, pats[i]);
        if (wcschr(pats[i], L'*') || wcschr(pats[i], L'?')) {
            WIN32_FIND_DATAW fd;
            HANDLE hf = FindFirstFileW(full, &fd);
            if (hf != INVALID_HANDLE_VALUE) {
                do {
                    if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
                    wchar_t item[MAX_PATH * 4];
                    swprintf(item, sizeof(item) / sizeof(item[0]), L"%ls\\%ls", dir, fd.cFileName);
                    run_for_body_with(ctx, var_char, item, body, io);
                } while (FindNextFileW(hf, &fd));
                FindClose(hf);
            }
        } else {
            run_for_body_with(ctx, var_char, full, body, io);
        }
    }

    wchar_t sub_pat[MAX_PATH * 4];
    swprintf(sub_pat, sizeof(sub_pat) / sizeof(sub_pat[0]), L"%ls\\*", dir);
    WIN32_FIND_DATAW fd;
    HANDLE hs = FindFirstFileW(sub_pat, &fd);
    if (hs != INVALID_HANDLE_VALUE) {
        do {
            if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
            if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) continue;
            wchar_t sub[MAX_PATH * 4];
            swprintf(sub, sizeof(sub) / sizeof(sub[0]), L"%ls\\%ls", dir, fd.cFileName);
            for_walk_rec(ctx, sub, var_char, pats, npats, body, io);
        } while (FindNextFileW(hs, &fd));
        FindClose(hs);
    }
}

static char *capture_cmd_output_utf8(shell_ctx_t *ctx, const wchar_t *cmd_text, const cmd_io_t *io) {
    SECURITY_ATTRIBUTES sa = { sizeof(sa), NULL, TRUE };
    HANDLE pr = NULL, pw = NULL;
    if (!CreatePipe(&pr, &pw, &sa, 0)) return NULL;
    SetHandleInformation(pr, HANDLE_FLAG_INHERIT, 0);

    cmd_io_t cap_io = { io->h_in, pw, io->h_err };
    exec_line_internal(ctx, cmd_text, &cap_io, ctx->batch_frame != NULL, false);
    CloseHandle(pw);

    size_t cap = 4096, len = 0;
    char *buf = (char *)malloc(cap);
    if (!buf) { CloseHandle(pr); return NULL; }
    DWORD rd = 0;
    while (ReadFile(pr, buf + len, (DWORD)(cap - len - 1), &rd, NULL) && rd > 0) {
        len += rd;
        if (cap - len < 1024) {
            cap *= 2;
            char *nb = (char *)realloc(buf, cap);
            if (!nb) break;
            buf = nb;
        }
    }
    buf[len] = '\0';
    CloseHandle(pr);
    return buf;
}

static int eval_for_f(shell_ctx_t *ctx, const cmd_node_t *node, const wchar_t *set_exp, const cmd_io_t *io) {
    wchar_t eol = L';';
    int skip = 0;
    wchar_t delims[64] = L" \t";
    bool usebackq = false;
    int tokens[16] = { 1 };
    int token_count = 1;
    bool star_rest = false;

    if (node->u.for_stmt.opts_or_root) {
        const wchar_t *op = node->u.for_stmt.opts_or_root;
        while (*op) {
            while (*op == L' ' || *op == L'\t') op++;
            if (!*op) break;
            if (_wcsnicmp(op, L"usebackq", 8) == 0) { usebackq = true; op += 8; }
            else if (_wcsnicmp(op, L"eol=", 4) == 0) { eol = op[4]; op += 5; }
            else if (_wcsnicmp(op, L"skip=", 5) == 0) { skip = _wtoi(op + 5); while (*op && !iswspace(*op)) op++; }
            else if (_wcsnicmp(op, L"delims=", 7) == 0) {
                op += 7;
                size_t di = 0;
                while (*op && !iswspace(*op) && di + 1 < 64) delims[di++] = *op++;
                delims[di] = L'\0';
            } else if (_wcsnicmp(op, L"tokens=", 7) == 0) {
                op += 7;
                token_count = 0;
                star_rest = false;
                while (*op && !iswspace(*op)) {
                    if (*op == L'*') { star_rest = true; op++; break; }
                    if (iswdigit(*op) && token_count < 16) {
                        tokens[token_count++] = _wtoi(op);
                        while (iswdigit(*op)) op++;
                    }
                    if (*op == L',') op++;
                    else if (*op != L'*' && !iswdigit(*op)) break;
                }
                if (token_count == 0 && star_rest) { tokens[0] = 1; token_count = 1; }
            } else {
                while (*op && !iswspace(*op)) op++;
            }
        }
    }

    const wchar_t *sp = cmd_skip_ws(set_exp);
    wchar_t *text_w = NULL;
    size_t slen = wcslen(sp);
    while (slen > 0 && iswspace(sp[slen - 1])) slen--;

    bool is_str = (!usebackq && slen >= 2 && sp[0] == L'"' && sp[slen - 1] == L'"');
    bool is_cmd = (!usebackq && slen >= 2 && sp[0] == L'\'' && sp[slen - 1] == L'\'') ||
                  (usebackq && slen >= 2 && sp[0] == L'`' && sp[slen - 1] == L'`');

    if (is_str) {
        text_w = (wchar_t *)malloc((slen - 1) * sizeof(wchar_t));
        if (text_w) { memcpy(text_w, sp + 1, (slen - 2) * sizeof(wchar_t)); text_w[slen - 2] = L'\0'; }
    } else if (is_cmd) {
        wchar_t *cmd_s = (wchar_t *)malloc((slen - 1) * sizeof(wchar_t));
        if (cmd_s) {
            memcpy(cmd_s, sp + 1, (slen - 2) * sizeof(wchar_t));
            cmd_s[slen - 2] = L'\0';
            char *utf8 = capture_cmd_output_utf8(ctx, cmd_s, io);
            free(cmd_s);
            if (utf8) {
                int wn = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, NULL, 0);
                if (wn > 0) {
                    text_w = (wchar_t *)malloc((size_t)wn * sizeof(wchar_t));
                    if (text_w) MultiByteToWideChar(CP_UTF8, 0, utf8, -1, text_w, wn);
                }
                free(utf8);
            }
        }
    } else {
        /* Read file */
        wchar_t fname[MAX_PATH * 4];
        wcsncpy(fname, sp, (sizeof(fname) / sizeof(fname[0])) - 1);
        fname[(sizeof(fname) / sizeof(fname[0])) - 1] = L'\0';
        cmd_trim_trailing_ws(fname);
        cmd_strip_outer_quotes(fname);
        HANDLE fh = CreateFileW(fname, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (fh != INVALID_HANDLE_VALUE) {
            LARGE_INTEGER fsz;
            if (GetFileSizeEx(fh, &fsz) && fsz.QuadPart < 16 * 1024 * 1024) {
                DWORD sz = (DWORD)fsz.QuadPart;
                char *raw = (char *)malloc((size_t)sz + 1);
                DWORD rd = 0;
                if (raw && (sz == 0 || ReadFile(fh, raw, sz, &rd, NULL))) {
                    raw[rd] = '\0';
                    int wn = MultiByteToWideChar(CP_UTF8, 0, raw, (int)rd, NULL, 0);
                    if (wn > 0) {
                        text_w = (wchar_t *)malloc(((size_t)wn + 1) * sizeof(wchar_t));
                        if (text_w) { MultiByteToWideChar(CP_UTF8, 0, raw, (int)rd, text_w, wn); text_w[wn] = L'\0'; }
                    }
                }
                free(raw);
            }
            CloseHandle(fh);
        }
    }

    if (!text_w) return ctx->last_exit_code;

    int line_no = 0;
    const wchar_t *p = text_w;
    while (*p) {
        const wchar_t *eolp = p;
        while (*eolp && *eolp != L'\n' && *eolp != L'\r') eolp++;
        size_t llen = (size_t)(eolp - p);
        wchar_t *ln = (wchar_t *)malloc((llen + 1) * sizeof(wchar_t));
        if (!ln) break;
        memcpy(ln, p, llen * sizeof(wchar_t));
        ln[llen] = L'\0';

        if (*eolp == L'\r' && eolp[1] == L'\n') p = eolp + 2;
        else if (*eolp) p = eolp + 1;
        else p = eolp;

        line_no++;
        if (line_no <= skip) { free(ln); continue; }
        if (ln[0] == L'\0' || (eol && ln[0] == eol)) { free(ln); continue; }

        /* Tokenize line by delims */
        const wchar_t *vals[17] = { 0 };
        wchar_t *dups[17] = { 0 };
        const wchar_t *lp = ln;
        int max_tok = tokens[token_count - 1];
        for (int ti = 1; ti <= max_tok + (star_rest ? 1 : 0); ti++) {
            while (*lp && wcschr(delims, *lp)) lp++;
            if (!*lp) break;
            if (ti == max_tok + 1 && star_rest) {
                dups[token_count] = _wcsdup(lp);
                vals[token_count] = dups[token_count];
                break;
            }
            const wchar_t *ts = lp;
            while (*lp && !wcschr(delims, *lp)) lp++;
            size_t tlen = (size_t)(lp - ts);
            for (int k = 0; k < token_count; k++) {
                if (tokens[k] == ti) {
                    dups[k] = (wchar_t *)malloc((tlen + 1) * sizeof(wchar_t));
                    if (dups[k]) { memcpy(dups[k], ts, tlen * sizeof(wchar_t)); dups[k][tlen] = L'\0'; }
                    vals[k] = dups[k];
                }
            }
        }

        if (vals[0]) {
            int total_bind = token_count + (star_rest ? 1 : 0);
            cmd_for_var_t frames[17];
            cmd_for_var_t *prev = (cmd_for_var_t *)ctx->for_vars;
            for (int k = 0; k < total_bind; k++) {
                frames[k].letter = (wchar_t)(node->u.for_stmt.var_char + k);
                frames[k].value = vals[k] ? vals[k] : L"";
                frames[k].prev = prev;
                prev = &frames[k];
            }
            ctx->for_vars = prev;
            eval_node(ctx, node->u.for_stmt.body, io);
            ctx->for_vars = frames[0].prev;
        }
        for (int k = 0; k < 17; k++) free(dups[k]);
        free(ln);
    }
    free(text_w);
    return ctx->last_exit_code;
}

static int eval_for(shell_ctx_t *ctx, const cmd_node_t *node, const cmd_io_t *io) {
    wchar_t *set_exp = expand_dynamic_tokens(ctx, node->u.for_stmt.set_spec);
    if (!set_exp) return 1;

    if (node->u.for_stmt.for_kind == FOR_KIND_FILE) {
        int rc = eval_for_f(ctx, node, set_exp, io);
        free(set_exp);
        return rc;
    }

    if (node->u.for_stmt.for_kind == FOR_KIND_NUM) {
        long start = 0, step = 1, end = 0;
        wchar_t **items = NULL;
        size_t n = 0;
        split_for_set(set_exp, &items, &n);
        if (n >= 1) start = wcstol(items[0], NULL, 0);
        if (n >= 2) step = wcstol(items[1], NULL, 0);
        if (n >= 3) end = wcstol(items[2], NULL, 0);
        for (size_t i = 0; i < n; i++) free(items[i]);
        free(items);
        free(set_exp);
        if (n < 3 || step == 0) return ctx->last_exit_code;

        for (long v = start; step > 0 ? (v <= end) : (v >= end); v += step) {
            wchar_t vbuf[32];
            swprintf(vbuf, 32, L"%ld", v);
            run_for_body_with(ctx, node->u.for_stmt.var_char, vbuf, node->u.for_stmt.body, io);
            if (ctx->should_exit) break;
        }
        return ctx->last_exit_code;
    }

    wchar_t **items = NULL;
    size_t n = 0;
    split_for_set(set_exp, &items, &n);
    free(set_exp);

    if (node->u.for_stmt.for_kind == FOR_KIND_REC) {
        wchar_t root[MAX_PATH * 4];
        if (node->u.for_stmt.opts_or_root && node->u.for_stmt.opts_or_root[0]) {
            GetFullPathNameW(node->u.for_stmt.opts_or_root, (DWORD)(sizeof(root) / sizeof(root[0])), root, NULL);
        } else {
            GetCurrentDirectoryW((DWORD)(sizeof(root) / sizeof(root[0])), root);
        }
        size_t rlen = wcslen(root);
        if (rlen > 3 && root[rlen - 1] == L'\\') root[rlen - 1] = L'\0';
        for_walk_rec(ctx, root, node->u.for_stmt.var_char, items, n, node->u.for_stmt.body, io);
    } else {
        bool dirs_only = (node->u.for_stmt.for_kind == FOR_KIND_DIR);
        for (size_t i = 0; i < n; i++) {
            if (wcschr(items[i], L'*') || wcschr(items[i], L'?')) {
                WIN32_FIND_DATAW fd;
                HANDLE hf = FindFirstFileW(items[i], &fd);
                if (hf != INVALID_HANDLE_VALUE) {
                    const wchar_t *sl = wcsrchr(items[i], L'\\');
                    size_t dlen = sl ? (size_t)(sl - items[i]) + 1 : 0;
                    do {
                        if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) continue;
                        bool is_d = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
                        if (dirs_only != is_d) continue;
                        wchar_t full[MAX_PATH * 4];
                        if (dlen > 0) {
                            swprintf(full, sizeof(full) / sizeof(full[0]), L"%.*ls%ls", (int)dlen, items[i], fd.cFileName);
                        } else {
                            wcsncpy(full, fd.cFileName, (sizeof(full) / sizeof(full[0])) - 1);
                            full[(sizeof(full) / sizeof(full[0])) - 1] = L'\0';
                        }
                        run_for_body_with(ctx, node->u.for_stmt.var_char, full, node->u.for_stmt.body, io);
                    } while (FindNextFileW(hf, &fd));
                    FindClose(hf);
                }
            } else if (!dirs_only) {
                run_for_body_with(ctx, node->u.for_stmt.var_char, items[i], node->u.for_stmt.body, io);
            }
        }
    }

    for (size_t i = 0; i < n; i++) free(items[i]);
    free(items);
    return ctx->last_exit_code;
}

static int eval_node(shell_ctx_t *ctx, const cmd_node_t *node, const cmd_io_t *io) {
    if (!node || ctx->should_exit) return ctx->last_exit_code;
    if (ctx->batch_frame) {
        const cmd_batch_frame_t *bf = (const cmd_batch_frame_t *)ctx->batch_frame;
        if (bf->exit_batch || bf->goto_pending) return ctx->last_exit_code;
    }

    switch (node->kind) {
        case CMD_NODE_REM:
            return ctx->last_exit_code;
        case CMD_NODE_SIMPLE:
            return eval_simple(ctx, node, io);
        case CMD_NODE_SEQ:
            eval_node(ctx, node->u.bin.left, io);
            return eval_node(ctx, node->u.bin.right, io);
        case CMD_NODE_AND: {
            int rc = eval_node(ctx, node->u.bin.left, io);
            if (rc == 0) return eval_node(ctx, node->u.bin.right, io);
            return rc;
        }
        case CMD_NODE_OR: {
            int rc = eval_node(ctx, node->u.bin.left, io);
            if (rc != 0) return eval_node(ctx, node->u.bin.right, io);
            return rc;
        }
        case CMD_NODE_PIPE:
            return eval_pipe(ctx, node->u.bin.left, node->u.bin.right, io);
        case CMD_NODE_BLOCK: {
            cmd_io_t local_io;
            redir_state_t rs;
            if (!apply_redirs(ctx, node->redirs, io, &local_io, &rs)) {
                close_redir_handles(&rs);
                ctx->last_exit_code = 1;
                return 1;
            }
            int rc = eval_node(ctx, node->u.block.body, &local_io);
            close_redir_handles(&rs);
            return rc;
        }
        case CMD_NODE_IF:
            return eval_if(ctx, node, io);
        case CMD_NODE_FOR:
            return eval_for(ctx, node, io);
    }
    return ctx->last_exit_code;
}

static int exec_line_internal(shell_ctx_t *ctx, const wchar_t *line, const cmd_io_t *io,
                              bool is_batch, bool is_top_interactive) {
    if (!ctx || !line) return 0;
    const wchar_t *p = cmd_skip_ws(line);
    if (!*p) return ctx->last_exit_code;

    if (is_top_interactive && try_exec_plumbus_builtin(ctx, p, io)) {
        return ctx->last_exit_code;
    }

    wchar_t *alias_exp = NULL;
    if (is_top_interactive && ctx->config_ptr) {
        alias_exp = config_expand_alias_line((const plumbus_config_t *)ctx->config_ptr, p);
        if (alias_exp) p = alias_exp;
    }

    wchar_t *pct_exp = cmd_expand_percent(ctx, p, is_batch);
    free(alias_exp);
    if (!pct_exp) return 1;

    wchar_t err_buf[256] = { 0 };
    cmd_node_t *ast = cmd_ast_parse(pct_exp, err_buf, sizeof(err_buf) / sizeof(err_buf[0]));
    free(pct_exp);

    if (!ast) {
        if (err_buf[0]) {
            cmd_io_printf(io->h_err, L"%ls\r\n", err_buf);
            ctx->last_exit_code = 1;
            return 1;
        }
        return ctx->last_exit_code;
    }

    int rc = eval_node(ctx, ast, io);
    cmd_ast_free(ast);
    return rc;
}

int cmd_runtime_exec_line(shell_ctx_t *ctx, const wchar_t *line, const cmd_io_t *io, bool is_top_interactive) {
    cmd_io_t fallback;
    if (!io) {
        fallback = cmd_io_std();
        io = &fallback;
    }
    return exec_line_internal(ctx, line, io, ctx && ctx->batch_frame != NULL, is_top_interactive);
}
