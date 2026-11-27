#ifndef PLUMBUS_PROMPT_H
#define PLUMBUS_PROMPT_H

#include "config.h"
#include "git_native.h"
#include <stddef.h>
#include <wchar.h>

/*
 * Builds ANSI-colored prompt string in heap buffer (caller frees with free()).
 * Abbreviates %USERPROFILE% to ~.
 */
wchar_t *prompt_render(const plumbus_config_t *cfg,
                       const wchar_t *cwd,
                       int last_exit_code,
                       const git_status_t *git);

/* Computes visible terminal column width by stripping ANSI CSI sequences */
size_t prompt_visible_width(const wchar_t *ansi_str);

/* Computes visible column width of the final line (after the last '\n') */
size_t prompt_last_line_visible_width(const wchar_t *ansi_str);

#endif /* PLUMBUS_PROMPT_H */
