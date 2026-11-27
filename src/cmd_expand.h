#ifndef PLUMBUS_CMD_EXPAND_H
#define PLUMBUS_CMD_EXPAND_H

#include "exec.h"
#include <stdbool.h>
#include <stddef.h>
#include <wchar.h>

typedef struct cmd_for_var {
    wchar_t letter;
    const wchar_t *value;
    struct cmd_for_var *prev;
} cmd_for_var_t;

typedef struct cmd_batch_frame {
    const wchar_t *script_path;
    int argc;
    wchar_t **argv;
    int shift_offset;
    const wchar_t *raw_tail;
    size_t local_stack_base;
    bool goto_pending;
    wchar_t goto_label[256];
    bool exit_batch;
    int exit_code;
    struct cmd_batch_frame *parent;
} cmd_batch_frame_t;

/*
 * Looks up an environment variable or cmd.exe dynamic pseudo-variable
 * (CD, ERRORLEVEL, CMDEXTVERSION, CMDCMDLINE, DATE, TIME, RANDOM, HIGHESTNUMANODENUMBER).
 * Caller must free(*out_val) when returning true.
 */
bool cmd_lookup_var(const shell_ctx_t *ctx, const wchar_t *name, wchar_t **out_val);

/*
 * Applies %VAR%, %VAR:old=new%, %VAR:~off,len%, and (when is_batch is true)
 * %0..%9, %*, %~dpnx0, and %% -> % expansion.
 * Caller must free() the returned string.
 */
wchar_t *cmd_expand_percent(const shell_ctx_t *ctx, const wchar_t *line, bool is_batch);

/*
 * Applies !VAR!, !VAR:old=new!, !VAR:~off,len! delayed expansion when ctx->delayed_expansion is true.
 * Caller must free() the returned string.
 */
wchar_t *cmd_expand_delayed(const shell_ctx_t *ctx, const wchar_t *text);

/*
 * Applies active FOR loop variable (%i, %~dpnxi) expansion from ctx->for_vars.
 * Caller must free() the returned string.
 */
wchar_t *cmd_expand_for_vars(const shell_ctx_t *ctx, const wchar_t *text);

/*
 * Formats a path/argument according to cmd.exe ~ modifiers (f,d,p,n,x,s,a,t,z).
 * Caller must free() the returned string.
 */
wchar_t *cmd_apply_tilde_mods(const wchar_t *raw_arg, const wchar_t *mods);

#endif /* PLUMBUS_CMD_EXPAND_H */
