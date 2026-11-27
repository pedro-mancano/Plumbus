#ifndef PLUMBUS_BUILTINS_H
#define PLUMBUS_BUILTINS_H

#include "exec.h"
#include <windows.h>
#include <stdbool.h>
#include <stddef.h>
#include <wchar.h>

typedef struct {
    HANDLE h_in;
    HANDLE h_out;
    HANDLE h_err;
} cmd_io_t;

cmd_io_t cmd_io_std(void);
void cmd_io_write_w(HANDLE h, const wchar_t *s, size_t len);
void cmd_io_write_sz(HANDLE h, const wchar_t *s);
void cmd_io_printf(HANDLE h, const wchar_t *fmt, ...);
bool cmd_io_read_line(HANDLE h, wchar_t *buf, size_t cap);

const wchar_t *cmd_skip_ws(const wchar_t *p);
void cmd_trim_trailing_ws(wchar_t *s);
void cmd_strip_outer_quotes(wchar_t *s);
void cmd_update_drive_cwd_env(const wchar_t *cwd);

bool cmd_parse_args(const wchar_t *tail, int *out_argc, wchar_t ***out_argv);
void cmd_free_args(int argc, wchar_t **argv);

/* Dispatches any of the 42 JumpTable built-ins (except GOTO/CALL/SHIFT/EXIT handled in runtime).
 * Sets *out_handled = true if cmd_word matched a built-in. */
int builtins_dispatch(shell_ctx_t *ctx, const wchar_t *cmd_word, const wchar_t *raw_tail,
                      const cmd_io_t *io, bool *out_handled);

void builtins_free_local_stack(shell_ctx_t *ctx);
void builtins_pop_locals_to(shell_ctx_t *ctx, size_t target_len);

/* Individual module entry points */
int builtin_dir(shell_ctx_t *ctx, const wchar_t *tail, const cmd_io_t *io);
int builtin_del(shell_ctx_t *ctx, const wchar_t *tail, const cmd_io_t *io);
int builtin_type(shell_ctx_t *ctx, const wchar_t *tail, const cmd_io_t *io);
int builtin_copy(shell_ctx_t *ctx, const wchar_t *tail, const cmd_io_t *io);
int builtin_cd(shell_ctx_t *ctx, const wchar_t *tail, const cmd_io_t *io);
int builtin_ren(shell_ctx_t *ctx, const wchar_t *tail, const cmd_io_t *io);
int builtin_mkdir(shell_ctx_t *ctx, const wchar_t *tail, const cmd_io_t *io);
int builtin_rmdir(shell_ctx_t *ctx, const wchar_t *tail, const cmd_io_t *io);
int builtin_move(shell_ctx_t *ctx, const wchar_t *tail, const cmd_io_t *io);
int builtin_pushd(shell_ctx_t *ctx, const wchar_t *tail, const cmd_io_t *io);
int builtin_popd(shell_ctx_t *ctx, const wchar_t *tail, const cmd_io_t *io);
int builtin_mklink(shell_ctx_t *ctx, const wchar_t *tail, const cmd_io_t *io);

int builtin_set(shell_ctx_t *ctx, const wchar_t *tail, const cmd_io_t *io);
int builtin_path(shell_ctx_t *ctx, const wchar_t *tail, const cmd_io_t *io);
int builtin_dpath(shell_ctx_t *ctx, const wchar_t *tail, const cmd_io_t *io);
int builtin_prompt(shell_ctx_t *ctx, const wchar_t *tail, const cmd_io_t *io);
int builtin_setlocal(shell_ctx_t *ctx, const wchar_t *tail, const cmd_io_t *io);
int builtin_endlocal(shell_ctx_t *ctx, const wchar_t *tail, const cmd_io_t *io);

int builtin_echo(shell_ctx_t *ctx, const wchar_t *tail, const cmd_io_t *io);
int builtin_pause(shell_ctx_t *ctx, const wchar_t *tail, const cmd_io_t *io);
int builtin_date(shell_ctx_t *ctx, const wchar_t *tail, const cmd_io_t *io);
int builtin_time(shell_ctx_t *ctx, const wchar_t *tail, const cmd_io_t *io);
int builtin_cls(shell_ctx_t *ctx, const wchar_t *tail, const cmd_io_t *io);
int builtin_verify(shell_ctx_t *ctx, const wchar_t *tail, const cmd_io_t *io);
int builtin_ver(shell_ctx_t *ctx, const wchar_t *tail, const cmd_io_t *io);
int builtin_vol(shell_ctx_t *ctx, const wchar_t *tail, const cmd_io_t *io);
int builtin_title(shell_ctx_t *ctx, const wchar_t *tail, const cmd_io_t *io);
int builtin_start(shell_ctx_t *ctx, const wchar_t *tail, const cmd_io_t *io);
int builtin_keys(shell_ctx_t *ctx, const wchar_t *tail, const cmd_io_t *io);
int builtin_assoc(shell_ctx_t *ctx, const wchar_t *tail, const cmd_io_t *io);
int builtin_ftype(shell_ctx_t *ctx, const wchar_t *tail, const cmd_io_t *io);
int builtin_break(shell_ctx_t *ctx, const wchar_t *tail, const cmd_io_t *io);
int builtin_color(shell_ctx_t *ctx, const wchar_t *tail, const cmd_io_t *io);

#endif /* PLUMBUS_BUILTINS_H */
