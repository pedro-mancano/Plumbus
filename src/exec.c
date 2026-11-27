#include "exec.h"
#include "builtins.h"
#include "cmd_runtime.h"
#include <windows.h>
#include <stdlib.h>
#include <string.h>

bool shell_ctx_init(shell_ctx_t *ctx, const cmd_options_t *opts, const wchar_t *backend_path) {
    if (!ctx) return false;
    memset(ctx, 0, sizeof(*ctx));
    if (backend_path) {
        wcsncpy(ctx->backend_path, backend_path, (sizeof(ctx->backend_path) / sizeof(ctx->backend_path[0])) - 1);
    }
    DWORD slen = GetModuleFileNameW(NULL, ctx->self_path, (DWORD)(sizeof(ctx->self_path) / sizeof(ctx->self_path[0])));
    if (slen == 0) return false;

    const wchar_t *cmdline = GetCommandLineW();
    if (cmdline) {
        wcsncpy(ctx->cmd_cmdline, cmdline, (sizeof(ctx->cmd_cmdline) / sizeof(ctx->cmd_cmdline[0])) - 1);
    }

    ctx->extensions_enabled = (opts && opts->extensions_set) ? opts->extensions : true;
    ctx->delayed_expansion = (opts && opts->delayed_expansion_set) ? opts->delayed_expansion : false;
    if (opts) {
        ctx->echo_off = opts->echo_off;
    }
    srand((unsigned)GetTickCount());
    return true;
}

void shell_ctx_free(shell_ctx_t *ctx) {
    if (!ctx) return;
    builtins_free_local_stack(ctx);
    for (size_t i = 0; i < ctx->dir_stack_len; i++) {
        free(ctx->dir_stack[i]);
    }
    free(ctx->dir_stack);
    ctx->dir_stack = NULL;
    ctx->dir_stack_len = 0;
    ctx->dir_stack_cap = 0;
}

bool exec_is_safe_for_sync_tail(const wchar_t *line) {
    if (!line) return false;
    const wchar_t *p = line;
    while (*p == L' ' || *p == L'\t') p++;
    size_t len = wcslen(p);
    if (len == 0) return false;

    size_t caret_run = 0;
    for (size_t i = len; i > 0 && p[i - 1] == L'^'; i--) {
        caret_run++;
    }
    if ((caret_run & 1u) != 0) return false;

    while (len > 0 && (p[len - 1] == L' ' || p[len - 1] == L'\t' || p[len - 1] == L'\r' || p[len - 1] == L'\n')) {
        len--;
    }
    if (len == 0) return false;

    wchar_t last = p[len - 1];
    if (last == L'&' || last == L'|' || last == L'<' || last == L'>') return false;

    size_t quote_count = 0;
    int paren_depth = 0;
    bool in_quotes = false;
    for (size_t i = 0; i < len; i++) {
        wchar_t c = p[i];
        if (c == L'"') {
            quote_count++;
            in_quotes = !in_quotes;
            continue;
        }
        if (!in_quotes) {
            if (c == L'^' && i + 1 < len) {
                i++;
                continue;
            }
            if (c == L'(') paren_depth++;
            else if (c == L')') {
                paren_depth--;
                if (paren_depth < 0) return false;
            }
        }
    }
    if ((quote_count & 1u) != 0) return false;
    if (paren_depth != 0) return false;
    return true;
}

int exec_interactive_line(shell_ctx_t *ctx, const wchar_t *line) {
    return cmd_runtime_exec_line(ctx, line, NULL, true);
}
