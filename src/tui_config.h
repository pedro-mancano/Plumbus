#ifndef PLUMBUS_TUI_CONFIG_H
#define PLUMBUS_TUI_CONFIG_H

#include "config.h"
#include "git_native.h"
#include <stdbool.h>
#include <stddef.h>
#include <wchar.h>

typedef enum {
    TUI_ITEM_THEME = 0,
    TUI_ITEM_NERD_FONTS,
    TUI_ITEM_SYNTAX_HL,
    TUI_ITEM_GHOST_TEXT,
    TUI_ITEM_GIT_STATUS,
    TUI_ITEM_ALIASES,
    TUI_ITEM_COUNT
} tui_cfg_item_t;

typedef struct {
    plumbus_config_t draft;
    int selected;
    bool in_alias_panel;
    int alias_selected;
    bool adding_alias;
    wchar_t alias_input[512];
    size_t alias_input_len;
    wchar_t status_msg[128];
} tui_cfg_state_t;

void tui_cfg_state_init(tui_cfg_state_t *st, const plumbus_config_t *src);
void tui_cfg_state_free(tui_cfg_state_t *st);

void tui_cfg_move_up(tui_cfg_state_t *st);
void tui_cfg_move_down(tui_cfg_state_t *st);
void tui_cfg_step_selected(tui_cfg_state_t *st, int delta);
void tui_cfg_toggle_selected(tui_cfg_state_t *st);
void tui_cfg_restore_defaults(tui_cfg_state_t *st);
bool tui_cfg_add_alias_expr(tui_cfg_state_t *st, const wchar_t *expr);
bool tui_cfg_remove_selected_alias(tui_cfg_state_t *st);

/*
 * Launches the full-screen interactive Plumbus Configurator in the Alternate Screen Buffer.
 * Returns true if user saved & applied changes to live_cfg and .cmdrc, false if canceled.
 */
bool tui_cfg_run(plumbus_config_t *live_cfg,
                 const wchar_t *cwd,
                 int last_exit_code,
                 const git_status_t *git_st);

#endif /* PLUMBUS_TUI_CONFIG_H */
