#include "builtins.h"
#include "cmd_expand.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wctype.h>

typedef struct {
    wchar_t *env_block;
    size_t env_block_chars;
    wchar_t cwd[MAX_PATH * 4];
    bool extensions_enabled;
    bool delayed_expansion;
} local_frame_t;

static int cmp_wstr_ci(const void *a, const void *b) {
    const wchar_t *sa = *(const wchar_t *const *)a;
    const wchar_t *sb = *(const wchar_t *const *)b;
    return _wcsicmp(sa, sb);
}

static int list_env_vars(const wchar_t *prefix, const cmd_io_t *io) {
    LPWCH block = GetEnvironmentStringsW();
    if (!block) return 1;

    size_t cap = 64, count = 0;
    const wchar_t **items = (const wchar_t **)malloc(cap * sizeof(const wchar_t *));
    if (!items) { FreeEnvironmentStringsW(block); return 1; }

    size_t plen = prefix ? wcslen(prefix) : 0;
    for (const wchar_t *p = block; *p; p += wcslen(p) + 1) {
        if (p[0] == L'=') continue; /* Skip hidden =C: drive vars */
        if (plen > 0 && _wcsnicmp(p, prefix, plen) != 0) continue;
        if (count == cap) {
            cap *= 2;
            const wchar_t **ni = (const wchar_t **)realloc(items, cap * sizeof(const wchar_t *));
            if (!ni) break;
            items = ni;
        }
        items[count++] = p;
    }

    qsort(items, count, sizeof(const wchar_t *), cmp_wstr_ci);
    for (size_t i = 0; i < count; i++) {
        cmd_io_printf(io->h_out, L"%ls\r\n", items[i]);
    }
    free(items);
    FreeEnvironmentStringsW(block);

    if (plen > 0 && count == 0) {
        cmd_io_printf(io->h_err, L"Environment variable %ls not defined\r\n", prefix);
        return 1;
    }
    return 0;
}

/* --- SET /A 32-bit signed arithmetic evaluator --- */
typedef struct {
    const wchar_t *p;
    bool err;
    const shell_ctx_t *ctx;
} arith_t;

static void arith_ws(arith_t *a) {
    while (*a->p == L' ' || *a->p == L'\t') a->p++;
}

static int32_t arith_comma(arith_t *a);

static int32_t arith_unary(arith_t *a) {
    arith_ws(a);
    if (*a->p == L'(') {
        a->p++;
        int32_t v = arith_comma(a);
        arith_ws(a);
        if (*a->p == L')') a->p++;
        else a->err = true;
        return v;
    }
    if (*a->p == L'+') { a->p++; return arith_unary(a); }
    if (*a->p == L'-') { a->p++; return -arith_unary(a); }
    if (*a->p == L'~') { a->p++; return ~arith_unary(a); }
    if (*a->p == L'!') { a->p++; return arith_unary(a) == 0 ? 1 : 0; }
    if (iswdigit(*a->p)) {
        wchar_t *end = NULL;
        long long val = wcstoll(a->p, &end, 0);
        if (end == a->p) { a->err = true; return 0; }
        a->p = end;
        return (int32_t)val;
    }
    if (iswalpha(*a->p) || *a->p == L'_') {
        const wchar_t *s = a->p;
        while (iswalnum(*a->p) || *a->p == L'_') a->p++;
        size_t len = (size_t)(a->p - s);
        wchar_t *name = (wchar_t *)malloc((len + 1) * sizeof(wchar_t));
        if (!name) { a->err = true; return 0; }
        memcpy(name, s, len * sizeof(wchar_t));
        name[len] = L'\0';
        wchar_t *val = NULL;
        int32_t res = 0;
        if (cmd_lookup_var(a->ctx, name, &val)) {
            res = (int32_t)wcstoll(val, NULL, 0);
            free(val);
        }
        free(name);
        return res;
    }
    a->err = true;
    return 0;
}

static int32_t arith_mul(arith_t *a) {
    int32_t v = arith_unary(a);
    for (;;) {
        arith_ws(a);
        wchar_t op = *a->p;
        if ((op == L'*' || op == L'/' || op == L'%') && a->p[1] != L'=') {
            a->p++;
            int32_t r = arith_unary(a);
            if (op == L'*') v *= r;
            else if (r == 0) { a->err = true; return 0; }
            else if (op == L'/') v /= r;
            else v %= r;
        } else break;
    }
    return v;
}

static int32_t arith_add(arith_t *a) {
    int32_t v = arith_mul(a);
    for (;;) {
        arith_ws(a);
        wchar_t op = *a->p;
        if ((op == L'+' || op == L'-') && a->p[1] != L'=') {
            a->p++;
            int32_t r = arith_mul(a);
            v = (op == L'+') ? (v + r) : (v - r);
        } else break;
    }
    return v;
}

static int32_t arith_shift(arith_t *a) {
    int32_t v = arith_add(a);
    for (;;) {
        arith_ws(a);
        if (a->p[0] == L'<' && a->p[1] == L'<' && a->p[2] != L'=') {
            a->p += 2;
            v <<= (arith_add(a) & 31);
        } else if (a->p[0] == L'>' && a->p[1] == L'>' && a->p[2] != L'=') {
            a->p += 2;
            v >>= (arith_add(a) & 31);
        } else break;
    }
    return v;
}

static int32_t arith_band(arith_t *a) {
    int32_t v = arith_shift(a);
    for (;;) {
        arith_ws(a);
        if (*a->p == L'&' && a->p[1] != L'=') { a->p++; v &= arith_shift(a); }
        else break;
    }
    return v;
}

static int32_t arith_bxor(arith_t *a) {
    int32_t v = arith_band(a);
    for (;;) {
        arith_ws(a);
        if (*a->p == L'^' && a->p[1] != L'=') { a->p++; v ^= arith_band(a); }
        else break;
    }
    return v;
}

static int32_t arith_bor(arith_t *a) {
    int32_t v = arith_bxor(a);
    for (;;) {
        arith_ws(a);
        if (*a->p == L'|' && a->p[1] != L'=') { a->p++; v |= arith_bxor(a); }
        else break;
    }
    return v;
}

static int32_t arith_assign(arith_t *a) {
    arith_ws(a);
    const wchar_t *save = a->p;
    if (iswalpha(*a->p) || *a->p == L'_') {
        const wchar_t *s = a->p;
        while (iswalnum(*a->p) || *a->p == L'_') a->p++;
        size_t len = (size_t)(a->p - s);
        arith_ws(a);
        int op = 0; /* 1:=, 2:+=, 3:-=, 4:*=, 5:/=, 6:%=, 7:&=, 8:^=, 9:|=, 10:<<=, 11:>>= */
        if (*a->p == L'=') { op = 1; a->p++; }
        else if (a->p[1] == L'=') {
            if (a->p[0] == L'+') op = 2;
            else if (a->p[0] == L'-') op = 3;
            else if (a->p[0] == L'*') op = 4;
            else if (a->p[0] == L'/') op = 5;
            else if (a->p[0] == L'%') op = 6;
            else if (a->p[0] == L'&') op = 7;
            else if (a->p[0] == L'^') op = 8;
            else if (a->p[0] == L'|') op = 9;
            if (op) a->p += 2;
        } else if (a->p[2] == L'=') {
            if (a->p[0] == L'<' && a->p[1] == L'<') { op = 10; a->p += 3; }
            else if (a->p[0] == L'>' && a->p[1] == L'>') { op = 11; a->p += 3; }
        }
        if (op) {
            wchar_t *name = (wchar_t *)malloc((len + 1) * sizeof(wchar_t));
            if (!name) { a->err = true; return 0; }
            memcpy(name, s, len * sizeof(wchar_t));
            name[len] = L'\0';
            int32_t rhs = arith_assign(a);
            if (!a->err) {
                int32_t cur = 0;
                if (op != 1) {
                    wchar_t *val = NULL;
                    if (cmd_lookup_var(a->ctx, name, &val)) {
                        cur = (int32_t)wcstoll(val, NULL, 0);
                        free(val);
                    }
                }
                int32_t nv = rhs;
                switch (op) {
                    case 2: nv = cur + rhs; break;
                    case 3: nv = cur - rhs; break;
                    case 4: nv = cur * rhs; break;
                    case 5: if (rhs == 0) a->err = true; else nv = cur / rhs; break;
                    case 6: if (rhs == 0) a->err = true; else nv = cur % rhs; break;
                    case 7: nv = cur & rhs; break;
                    case 8: nv = cur ^ rhs; break;
                    case 9: nv = cur | rhs; break;
                    case 10: nv = cur << (rhs & 31); break;
                    case 11: nv = cur >> (rhs & 31); break;
                    default: break;
                }
                if (!a->err) {
                    wchar_t buf[32];
                    swprintf(buf, 32, L"%d", (int)nv);
                    SetEnvironmentVariableW(name, buf);
                }
                free(name);
                return nv;
            }
            free(name);
            return 0;
        }
    }
    a->p = save;
    return arith_bor(a);
}

static int32_t arith_comma(arith_t *a) {
    int32_t v = arith_assign(a);
    for (;;) {
        arith_ws(a);
        if (*a->p == L',') {
            a->p++;
            v = arith_assign(a);
        } else break;
    }
    return v;
}

int builtin_set(shell_ctx_t *ctx, const wchar_t *tail, const cmd_io_t *io) {
    const wchar_t *p = cmd_skip_ws(tail);
    if (!*p) {
        return list_env_vars(NULL, io);
    }

    if (_wcsnicmp(p, L"/a", 2) == 0 && (p[2] == L'\0' || p[2] == L' ' || p[2] == L'\t')) {
        p = cmd_skip_ws(p + 2);
        if (!*p) {
            cmd_io_write_sz(io->h_err, L"The syntax of the command is incorrect.\r\n");
            return 1;
        }
        wchar_t *expr = _wcsdup(p);
        if (!expr) return 1;
        cmd_trim_trailing_ws(expr);
        cmd_strip_outer_quotes(expr);
        arith_t a = { expr, false, ctx };
        int32_t res = arith_comma(&a);
        arith_ws(&a);
        if (a.err || *a.p != L'\0') {
            free(expr);
            cmd_io_write_sz(io->h_err, L"Invalid number.  Numbers are limited to 32-bits of precision.\r\n");
            return 1;
        }
        free(expr);
        if (!ctx->batch_frame) {
            cmd_io_printf(io->h_out, L"%d", (int)res);
        }
        return 0;
    }

    if (_wcsnicmp(p, L"/p", 2) == 0 && (p[2] == L'\0' || p[2] == L' ' || p[2] == L'\t')) {
        p = cmd_skip_ws(p + 2);
        wchar_t *arg = _wcsdup(p);
        if (!arg) return 1;
        cmd_trim_trailing_ws(arg);
        cmd_strip_outer_quotes(arg);
        wchar_t *eq = wcschr(arg, L'=');
        if (!eq || eq == arg) {
            free(arg);
            cmd_io_write_sz(io->h_err, L"The syntax of the command is incorrect.\r\n");
            return 1;
        }
        *eq = L'\0';
        const wchar_t *prompt_str = eq + 1;
        if (*prompt_str) cmd_io_write_sz(io->h_out, prompt_str);
        wchar_t line[4096];
        if (!cmd_io_read_line(io->h_in, line, sizeof(line) / sizeof(line[0]))) {
            free(arg);
            return 1;
        }
        SetEnvironmentVariableW(arg, line[0] ? line : NULL);
        free(arg);
        return 0;
    }

    wchar_t *work = _wcsdup(p);
    if (!work) return 1;
    if (work[0] == L'"') {
        wchar_t *last_q = wcsrchr(work + 1, L'"');
        if (last_q) {
            *last_q = L'\0';
            memmove(work, work + 1, (size_t)(last_q - work) * sizeof(wchar_t));
        }
    }

    wchar_t *eq = wcschr(work, L'=');
    if (!eq) {
        cmd_trim_trailing_ws(work);
        int rc = list_env_vars(work, io);
        free(work);
        return rc;
    }
    if (eq == work) {
        free(work);
        cmd_io_write_sz(io->h_err, L"The syntax of the command is incorrect.\r\n");
        return 1;
    }
    *eq = L'\0';
    const wchar_t *val = eq + 1;
    BOOL ok = SetEnvironmentVariableW(work, val[0] ? val : NULL);
    free(work);
    return ok ? 0 : 1;
}

static int path_common(const wchar_t *var_name, const wchar_t *empty_msg,
                       const wchar_t *tail, const cmd_io_t *io) {
    const wchar_t *p = cmd_skip_ws(tail);
    if (!*p) {
        wchar_t *val = NULL;
        if (cmd_lookup_var(NULL, var_name, &val)) {
            cmd_io_printf(io->h_out, L"%ls=%ls\r\n", var_name, val);
            free(val);
        } else {
            cmd_io_write_sz(io->h_out, empty_msg);
        }
        return 0;
    }
    wchar_t *w = _wcsdup(p);
    if (!w) return 1;
    cmd_trim_trailing_ws(w);
    if (wcscmp(w, L";") == 0) {
        SetEnvironmentVariableW(var_name, NULL);
    } else {
        SetEnvironmentVariableW(var_name, w);
    }
    free(w);
    return 0;
}

int builtin_path(shell_ctx_t *ctx, const wchar_t *tail, const cmd_io_t *io) {
    (void)ctx;
    return path_common(L"PATH", L"(null)\r\n", tail, io);
}

int builtin_dpath(shell_ctx_t *ctx, const wchar_t *tail, const cmd_io_t *io) {
    (void)ctx;
    return path_common(L"DPATH", L"No Path\r\n", tail, io);
}

int builtin_prompt(shell_ctx_t *ctx, const wchar_t *tail, const cmd_io_t *io) {
    (void)ctx;
    (void)io;
    const wchar_t *p = cmd_skip_ws(tail);
    if (!*p) {
        SetEnvironmentVariableW(L"PROMPT", NULL);
        return 0;
    }
    SetEnvironmentVariableW(L"PROMPT", p);
    return 0;
}

int builtin_setlocal(shell_ctx_t *ctx, const wchar_t *tail, const cmd_io_t *io) {
    (void)io;
    if (!ctx) return 1;

    LPWCH raw = GetEnvironmentStringsW();
    if (!raw) return 1;
    size_t total_chars = 0;
    while (raw[total_chars] != L'\0') {
        total_chars += wcslen(raw + total_chars) + 1;
    }
    total_chars++; /* final double null */

    local_frame_t frame;
    memset(&frame, 0, sizeof(frame));
    frame.env_block = (wchar_t *)malloc(total_chars * sizeof(wchar_t));
    if (!frame.env_block) { FreeEnvironmentStringsW(raw); return 1; }
    memcpy(frame.env_block, raw, total_chars * sizeof(wchar_t));
    frame.env_block_chars = total_chars;
    FreeEnvironmentStringsW(raw);

    if (GetCurrentDirectoryW((DWORD)(sizeof(frame.cwd) / sizeof(frame.cwd[0])), frame.cwd) == 0) {
        frame.cwd[0] = L'\0';
    }
    frame.extensions_enabled = ctx->extensions_enabled;
    frame.delayed_expansion = ctx->delayed_expansion;

    if (ctx->local_stack_len == ctx->local_stack_cap) {
        size_t ncap = ctx->local_stack_cap ? ctx->local_stack_cap * 2 : 8;
        local_frame_t *ns = (local_frame_t *)realloc(ctx->local_stack, ncap * sizeof(local_frame_t));
        if (!ns) { free(frame.env_block); return 1; }
        ctx->local_stack = ns;
        ctx->local_stack_cap = ncap;
    }
    ((local_frame_t *)ctx->local_stack)[ctx->local_stack_len++] = frame;

    int argc = 0;
    wchar_t **argv = NULL;
    if (cmd_parse_args(tail, &argc, &argv)) {
        for (int i = 0; i < argc; i++) {
            if (_wcsicmp(argv[i], L"ENABLEEXTENSIONS") == 0) ctx->extensions_enabled = true;
            else if (_wcsicmp(argv[i], L"DISABLEEXTENSIONS") == 0) ctx->extensions_enabled = false;
            else if (_wcsicmp(argv[i], L"ENABLEDELAYEDEXPANSION") == 0) ctx->delayed_expansion = true;
            else if (_wcsicmp(argv[i], L"DISABLEDELAYEDEXPANSION") == 0) ctx->delayed_expansion = false;
        }
        cmd_free_args(argc, argv);
    }
    return 0;
}

static void restore_env_block(const wchar_t *saved_block) {
    /* Clear all current env vars first */
    LPWCH cur = GetEnvironmentStringsW();
    if (cur) {
        size_t cap = 64, n = 0;
        wchar_t **names = (wchar_t **)malloc(cap * sizeof(wchar_t *));
        if (names) {
            for (const wchar_t *p = cur; *p; p += wcslen(p) + 1) {
                const wchar_t *eq = wcschr(p + (p[0] == L'=' ? 1 : 0), L'=');
                if (!eq) continue;
                size_t klen = (size_t)(eq - p);
                wchar_t *k = (wchar_t *)malloc((klen + 1) * sizeof(wchar_t));
                if (!k) continue;
                memcpy(k, p, klen * sizeof(wchar_t));
                k[klen] = L'\0';
                if (n == cap) {
                    cap *= 2;
                    wchar_t **nn = (wchar_t **)realloc(names, cap * sizeof(wchar_t *));
                    if (!nn) { free(k); break; }
                    names = nn;
                }
                names[n++] = k;
            }
            for (size_t i = 0; i < n; i++) {
                SetEnvironmentVariableW(names[i], NULL);
                free(names[i]);
            }
            free(names);
        }
        FreeEnvironmentStringsW(cur);
    }

    for (const wchar_t *p = saved_block; *p; p += wcslen(p) + 1) {
        const wchar_t *eq = wcschr(p + (p[0] == L'=' ? 1 : 0), L'=');
        if (!eq) continue;
        size_t klen = (size_t)(eq - p);
        wchar_t *k = (wchar_t *)malloc((klen + 1) * sizeof(wchar_t));
        if (!k) continue;
        memcpy(k, p, klen * sizeof(wchar_t));
        k[klen] = L'\0';
        SetEnvironmentVariableW(k, eq + 1);
        free(k);
    }
}

int builtin_endlocal(shell_ctx_t *ctx, const wchar_t *tail, const cmd_io_t *io) {
    (void)tail;
    (void)io;
    if (!ctx || ctx->local_stack_len == 0) return 0;

    local_frame_t *stack = (local_frame_t *)ctx->local_stack;
    local_frame_t frame = stack[--ctx->local_stack_len];

    restore_env_block(frame.env_block);
    free(frame.env_block);

    if (frame.cwd[0]) {
        SetCurrentDirectoryW(frame.cwd);
    }
    ctx->extensions_enabled = frame.extensions_enabled;
    ctx->delayed_expansion = frame.delayed_expansion;
    return 0;
}

void builtins_pop_locals_to(shell_ctx_t *ctx, size_t target_len) {
    if (!ctx) return;
    cmd_io_t io = cmd_io_std();
    while (ctx->local_stack_len > target_len) {
        builtin_endlocal(ctx, L"", &io);
    }
}

void builtins_free_local_stack(shell_ctx_t *ctx) {
    if (!ctx || !ctx->local_stack) return;
    local_frame_t *stack = (local_frame_t *)ctx->local_stack;
    for (size_t i = 0; i < ctx->local_stack_len; i++) {
        free(stack[i].env_block);
    }
    free(ctx->local_stack);
    ctx->local_stack = NULL;
    ctx->local_stack_len = 0;
    ctx->local_stack_cap = 0;
}
