#ifndef PLUMBUS_CONFIG_H
#define PLUMBUS_CONFIG_H

#include <stdbool.h>
#include <stddef.h>
#include <wchar.h>

typedef enum {
    THEME_CMD = 0,
    THEME_ROBBYRUSSELL,
    THEME_AGNOSTER,
    THEME_OMB_BOX,
    THEME_PURE,
    THEME_DRACULA,
    THEME_MINIMAL,
    THEME_COUNT
} plumbus_theme_t;

typedef struct {
    wchar_t *name;
    wchar_t *value;
} plumbus_alias_t;

typedef struct {
    plumbus_theme_t theme;
    bool nerd_fonts;
    bool syntax_highlighting;
    bool ghost_text;
    bool git_status;
    plumbus_alias_t *aliases;
    size_t alias_count;
    size_t alias_cap;
} plumbus_config_t;

void config_init(plumbus_config_t *cfg);
void config_free(plumbus_config_t *cfg);
void config_reset_defaults(plumbus_config_t *cfg);
bool config_copy(plumbus_config_t *dst, const plumbus_config_t *src);

const wchar_t *config_theme_name(plumbus_theme_t theme);
bool config_theme_from_name(const wchar_t *name, plumbus_theme_t *out_theme);

/* Loads PLUMBUS_RC_FILE override or %USERPROFILE%\.cmdrc (returns true even if file absent) */
bool config_load_rc(plumbus_config_t *cfg);

/* Saves preferences and aliases in clean commented UTF-8 format to .cmdrc */
bool config_save(const plumbus_config_t *cfg);

bool config_set_alias(plumbus_config_t *cfg, const wchar_t *name, const wchar_t *value);
bool config_remove_alias(plumbus_config_t *cfg, const wchar_t *name);
const wchar_t *config_find_alias(const plumbus_config_t *cfg, const wchar_t *name);

/* Expands leading alias token if present; returns heap string or NULL if no alias matched */
wchar_t *config_expand_alias_line(const plumbus_config_t *cfg, const wchar_t *line);

#endif /* PLUMBUS_CONFIG_H */
