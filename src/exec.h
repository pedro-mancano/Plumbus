#ifndef PLUMBUS_EXEC_H
#define PLUMBUS_EXEC_H

#include "cmdline.h"
#include <stdbool.h>
#include <stddef.h>
#include <wchar.h>

typedef struct {
    wchar_t backend_path[32768];
    wchar_t self_path[32768];
    wchar_t cmd_cmdline[4096];
    int last_exit_code;
    bool should_exit;
    bool echo_off;
    bool extensions_enabled;
    bool delayed_expansion;
    bool verify_on;
    bool keys_on;
    wchar_t **dir_stack;
    size_t dir_stack_len;
    size_t dir_stack_cap;
    void *local_stack;
    size_t local_stack_len;
    size_t local_stack_cap;
    void *batch_frame;
    void *for_vars;
    void *config_ptr; /* Reserved for Phase 3 config_t* */
} shell_ctx_t;

bool shell_ctx_init(shell_ctx_t *ctx, const cmd_options_t *opts, const wchar_t *backend_path);
void shell_ctx_free(shell_ctx_t *ctx);

/*
 * Kept for backwards compatibility with existing unit tests.
 */
bool exec_is_safe_for_sync_tail(const wchar_t *line);

/*
 * Executes one command line natively via the standalone C17 engine,
 * updating ctx->last_exit_code and persisting CWD + ENV changes.
 */
int exec_interactive_line(shell_ctx_t *ctx, const wchar_t *line);

#endif /* PLUMBUS_EXEC_H */
