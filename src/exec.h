#ifndef PLUMBUS_EXEC_H
#define PLUMBUS_EXEC_H

#include "cmdline.h"
#include <stdbool.h>
#include <stddef.h>
#include <wchar.h>

typedef struct {
    wchar_t backend_path[32768];
    wchar_t self_path[32768];
    int last_exit_code;
    bool should_exit;
    bool echo_off;
    wchar_t **dir_stack;
    size_t dir_stack_len;
    size_t dir_stack_cap;
    void *config_ptr; /* Reserved for Phase 3 config_t* */
} shell_ctx_t;

bool shell_ctx_init(shell_ctx_t *ctx, const cmd_options_t *opts, const wchar_t *backend_path);
void shell_ctx_free(shell_ctx_t *ctx);

/*
 * Executes one interactive command line using hybrid fast-path / direct CreateProcessW /
 * Sync Bridge, updating ctx->last_exit_code and persisting CWD + ENV changes.
 */
int exec_interactive_line(shell_ctx_t *ctx, const wchar_t *line);

#endif /* PLUMBUS_EXEC_H */
