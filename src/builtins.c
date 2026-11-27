#include "builtins.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wctype.h>

cmd_io_t cmd_io_std(void) {
    cmd_io_t io;
    io.h_in = GetStdHandle(STD_INPUT_HANDLE);
    io.h_out = GetStdHandle(STD_OUTPUT_HANDLE);
    io.h_err = GetStdHandle(STD_ERROR_HANDLE);
    return io;
}

void cmd_io_write_w(HANDLE h, const wchar_t *s, size_t len) {
    if (!h || h == INVALID_HANDLE_VALUE || !s || len == 0) return;
    DWORD mode = 0;
    if (GetConsoleMode(h, &mode)) {
        DWORD written = 0;
        WriteConsoleW(h, s, (DWORD)len, &written, NULL);
        return;
    }
    int n = WideCharToMultiByte(CP_UTF8, 0, s, (int)len, NULL, 0, NULL, NULL);
    if (n <= 0) return;
    char stack_buf[1024];
    char *buf = (n <= (int)sizeof(stack_buf)) ? stack_buf : (char *)malloc((size_t)n);
    if (!buf) return;
    WideCharToMultiByte(CP_UTF8, 0, s, (int)len, buf, n, NULL, NULL);
    DWORD written = 0;
    WriteFile(h, buf, (DWORD)n, &written, NULL);
    if (buf != stack_buf) free(buf);
}

void cmd_io_write_sz(HANDLE h, const wchar_t *s) {
    if (s) cmd_io_write_w(h, s, wcslen(s));
}

void cmd_io_printf(HANDLE h, const wchar_t *fmt, ...) {
    if (!fmt) return;
    wchar_t stack_buf[1024];
    va_list ap;
    va_start(ap, fmt);
    int n = _vsnwprintf(stack_buf, sizeof(stack_buf) / sizeof(stack_buf[0]) - 1, fmt, ap);
    va_end(ap);
    if (n >= 0) {
        stack_buf[n] = L'\0';
        cmd_io_write_w(h, stack_buf, (size_t)n);
        return;
    }
    size_t cap = 4096;
    for (;;) {
        wchar_t *buf = (wchar_t *)malloc(cap * sizeof(wchar_t));
        if (!buf) return;
        va_start(ap, fmt);
        n = _vsnwprintf(buf, cap - 1, fmt, ap);
        va_end(ap);
        if (n >= 0) {
            buf[n] = L'\0';
            cmd_io_write_w(h, buf, (size_t)n);
            free(buf);
            return;
        }
        free(buf);
        if (cap >= 1048576) return;
        cap *= 2;
    }
}

bool cmd_io_read_line(HANDLE h, wchar_t *buf, size_t cap) {
    if (!h || h == INVALID_HANDLE_VALUE || !buf || cap < 2) return false;
    DWORD mode = 0;
    if (GetConsoleMode(h, &mode)) {
        DWORD rd = 0;
        if (!ReadConsoleW(h, buf, (DWORD)(cap - 1), &rd, NULL)) return false;
        buf[rd] = L'\0';
        while (rd > 0 && (buf[rd - 1] == L'\n' || buf[rd - 1] == L'\r')) buf[--rd] = L'\0';
        return true;
    }
    char raw[4096];
    size_t ri = 0;
    DWORD rd = 0;
    char ch = 0;
    while (ri + 1 < sizeof(raw) && ReadFile(h, &ch, 1, &rd, NULL) && rd == 1) {
        if (ch == '\n') break;
        if (ch != '\r') raw[ri++] = ch;
    }
    if (ri == 0 && rd == 0) return false;
    raw[ri] = '\0';
    int wn = MultiByteToWideChar(CP_UTF8, 0, raw, -1, buf, (int)cap);
    if (wn <= 0) {
        MultiByteToWideChar(CP_ACP, 0, raw, -1, buf, (int)cap);
    }
    buf[cap - 1] = L'\0';
    return true;
}

const wchar_t *cmd_skip_ws(const wchar_t *p) {
    if (!p) return L"";
    while (*p == L' ' || *p == L'\t') p++;
    return p;
}

void cmd_trim_trailing_ws(wchar_t *s) {
    if (!s) return;
    size_t n = wcslen(s);
    while (n > 0 && (s[n - 1] == L' ' || s[n - 1] == L'\t' || s[n - 1] == L'\r' || s[n - 1] == L'\n')) {
        s[--n] = L'\0';
    }
}

void cmd_strip_outer_quotes(wchar_t *s) {
    if (!s) return;
    size_t n = wcslen(s);
    if (n >= 2 && s[0] == L'"' && s[n - 1] == L'"') {
        memmove(s, s + 1, (n - 2) * sizeof(wchar_t));
        s[n - 2] = L'\0';
    }
}

void cmd_update_drive_cwd_env(const wchar_t *cwd) {
    if (cwd && iswalpha(cwd[0]) && cwd[1] == L':') {
        wchar_t name[4] = { L'=', towupper(cwd[0]), L':', L'\0' };
        SetEnvironmentVariableW(name, cwd);
    }
}

bool cmd_parse_args(const wchar_t *tail, int *out_argc, wchar_t ***out_argv) {
    *out_argc = 0;
    *out_argv = NULL;
    if (!tail) return true;

    int cap = 16, argc = 0;
    wchar_t **argv = (wchar_t **)malloc((size_t)cap * sizeof(wchar_t *));
    if (!argv) return false;

    const wchar_t *p = tail;
    for (;;) {
        while (*p == L' ' || *p == L'\t') p++;
        if (!*p) break;

        size_t tcap = 64, tlen = 0;
        wchar_t *tok = (wchar_t *)malloc(tcap * sizeof(wchar_t));
        if (!tok) { cmd_free_args(argc, argv); return false; }

        bool in_q = false;
        while (*p) {
            if (*p == L'"') {
                in_q = !in_q;
                p++;
                continue;
            }
            if (!in_q && (*p == L' ' || *p == L'\t')) break;
            if (tlen + 2 >= tcap) {
                tcap *= 2;
                wchar_t *nt = (wchar_t *)realloc(tok, tcap * sizeof(wchar_t));
                if (!nt) { free(tok); cmd_free_args(argc, argv); return false; }
                tok = nt;
            }
            tok[tlen++] = *p++;
        }
        tok[tlen] = L'\0';

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

void cmd_free_args(int argc, wchar_t **argv) {
    if (!argv) return;
    for (int i = 0; i < argc; i++) free(argv[i]);
    free(argv);
}

typedef int (*builtin_fn_t)(shell_ctx_t *ctx, const wchar_t *tail, const cmd_io_t *io);

typedef struct {
    const wchar_t *name;
    builtin_fn_t fn;
} builtin_entry_t;

static const builtin_entry_t g_builtins[] = {
    { L"DIR",      builtin_dir },
    { L"ERASE",    builtin_del },
    { L"DEL",      builtin_del },
    { L"TYPE",     builtin_type },
    { L"COPY",     builtin_copy },
    { L"CD",       builtin_cd },
    { L"CHDIR",    builtin_cd },
    { L"RENAME",   builtin_ren },
    { L"REN",      builtin_ren },
    { L"ECHO",     builtin_echo },
    { L"SET",      builtin_set },
    { L"PAUSE",    builtin_pause },
    { L"DATE",     builtin_date },
    { L"TIME",     builtin_time },
    { L"PROMPT",   builtin_prompt },
    { L"MD",       builtin_mkdir },
    { L"MKDIR",    builtin_mkdir },
    { L"RD",       builtin_rmdir },
    { L"RMDIR",    builtin_rmdir },
    { L"PATH",     builtin_path },
    { L"CLS",      builtin_cls },
    { L"VERIFY",   builtin_verify },
    { L"VER",      builtin_ver },
    { L"VOL",      builtin_vol },
    { L"SETLOCAL", builtin_setlocal },
    { L"ENDLOCAL", builtin_endlocal },
    { L"TITLE",    builtin_title },
    { L"START",    builtin_start },
    { L"DPATH",    builtin_dpath },
    { L"KEYS",     builtin_keys },
    { L"MOVE",     builtin_move },
    { L"PUSHD",    builtin_pushd },
    { L"POPD",     builtin_popd },
    { L"ASSOC",    builtin_assoc },
    { L"FTYPE",    builtin_ftype },
    { L"BREAK",    builtin_break },
    { L"COLOR",    builtin_color },
    { L"MKLINK",   builtin_mklink }
};

int builtins_dispatch(shell_ctx_t *ctx, const wchar_t *cmd_word, const wchar_t *raw_tail,
                      const cmd_io_t *io, bool *out_handled) {
    *out_handled = false;
    if (!cmd_word || !*cmd_word) return 0;

    /* Drive switch X: */
    if (iswalpha(cmd_word[0]) && cmd_word[1] == L':' && cmd_word[2] == L'\0' &&
        *cmd_skip_ws(raw_tail) == L'\0') {
        *out_handled = true;
        wchar_t dvar[4] = { L'=', towupper(cmd_word[0]), L':', L'\0' };
        wchar_t target[MAX_PATH * 4];
        DWORD n = GetEnvironmentVariableW(dvar, target, (DWORD)(sizeof(target) / sizeof(target[0])));
        if (n == 0 || n >= sizeof(target) / sizeof(target[0])) {
            target[0] = towupper(cmd_word[0]);
            target[1] = L':';
            target[2] = L'\\';
            target[3] = L'\0';
        }
        if (!SetCurrentDirectoryW(target)) {
            cmd_io_write_sz(io->h_err, L"The system cannot find the drive specified.\r\n");
            ctx->last_exit_code = 1;
            return 1;
        }
        ctx->last_exit_code = 0;
        return 0;
    }

    for (size_t i = 0; i < sizeof(g_builtins) / sizeof(g_builtins[0]); i++) {
        if (_wcsicmp(cmd_word, g_builtins[i].name) == 0) {
            *out_handled = true;
            int rc = g_builtins[i].fn(ctx, raw_tail ? raw_tail : L"", io);
            ctx->last_exit_code = rc;
            return rc;
        }
    }
    return 0;
}
