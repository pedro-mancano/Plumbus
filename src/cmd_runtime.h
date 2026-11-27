#ifndef PLUMBUS_CMD_RUNTIME_H
#define PLUMBUS_CMD_RUNTIME_H

#include "exec.h"
#include "builtins.h"
#include <stdbool.h>
#include <wchar.h>

/*
 * Executes one command line via the standalone C17 engine (expansion, AST,
 * redirections, pipes, IF/FOR, built-ins, external executables, and .bat/.cmd).
 */
int cmd_runtime_exec_line(shell_ctx_t *ctx, const wchar_t *line, const cmd_io_t *io, bool is_top_interactive);

/*
 * Executes a .bat / .cmd script natively with full label/GOTO/CALL/SHIFT/SETLOCAL support.
 */
int cmd_runtime_exec_batch_file(shell_ctx_t *ctx, const wchar_t *script_path,
                                const wchar_t *raw_tail, const cmd_io_t *io);

#endif /* PLUMBUS_CMD_RUNTIME_H */
