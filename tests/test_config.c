#include "test_framework.h"
#include "config.h"
#include "tui_config.h"
#include <windows.h>
#include <string.h>

TEST_CASE(test_config_cmdrc_aliases_theme_and_expansion) {
    wchar_t tmp[MAX_PATH], rc[MAX_PATH];
    GetTempPathW(MAX_PATH, tmp);
    swprintf(rc, MAX_PATH, L"%lsplumbus_test.cmdrc", tmp);

    const char *content =
        "# Sample .cmdrc\n"
        "alias ll=\"dir /a\"\n"
        "alias gs='git status'\n"
        "theme=agnoster\n"
        "nerd_fonts=off\n"
        "syntax_highlighting=off\n"
        "ghost_text=off\n"
        "git_status=off\n"
        "set PLUMBUS_RC_VAR=ok_rc\n";

    HANDLE f = CreateFileW(rc, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    ASSERT_TRUE(f != INVALID_HANDLE_VALUE);
    DWORD w = 0;
    WriteFile(f, content, (DWORD)strlen(content), &w, NULL);
    CloseHandle(f);

    SetEnvironmentVariableW(L"PLUMBUS_RC_FILE", rc);
    plumbus_config_t cfg;
    config_init(&cfg);
    ASSERT_TRUE(config_load_rc(&cfg));

    ASSERT_EQ_INT(THEME_AGNOSTER, cfg.theme);
    ASSERT_TRUE(!cfg.nerd_fonts);
    ASSERT_TRUE(!cfg.syntax_highlighting);
    ASSERT_TRUE(!cfg.ghost_text);
    ASSERT_TRUE(!cfg.git_status);
    ASSERT_EQ_WSTR(L"dir /a", config_find_alias(&cfg, L"ll"));
    ASSERT_EQ_WSTR(L"git status", config_find_alias(&cfg, L"gs"));

    wchar_t *exp = config_expand_alias_line(&cfg, L"ll /b");
    ASSERT_TRUE(exp != NULL);
    ASSERT_EQ_WSTR(L"dir /a /b", exp);
    free(exp);

    wchar_t val[64];
    ASSERT_TRUE(GetEnvironmentVariableW(L"PLUMBUS_RC_VAR", val, 64) > 0);
    ASSERT_EQ_WSTR(L"ok_rc", val);

    ASSERT_TRUE(config_remove_alias(&cfg, L"ll"));
    ASSERT_TRUE(config_find_alias(&cfg, L"ll") == NULL);

    /* Round-trip save and reload */
    cfg.nerd_fonts = true;
    ASSERT_TRUE(config_save(&cfg));

    plumbus_config_t reloaded;
    config_init(&reloaded);
    ASSERT_TRUE(config_load_rc(&reloaded));
    ASSERT_EQ_INT(THEME_AGNOSTER, reloaded.theme);
    ASSERT_TRUE(reloaded.nerd_fonts);
    ASSERT_TRUE(!reloaded.syntax_highlighting);
    ASSERT_TRUE(!reloaded.ghost_text);
    ASSERT_TRUE(!reloaded.git_status);
    ASSERT_EQ_WSTR(L"git status", config_find_alias(&reloaded, L"gs"));

    config_free(&reloaded);
    config_free(&cfg);
    SetEnvironmentVariableW(L"PLUMBUS_RC_FILE", NULL);
    SetEnvironmentVariableW(L"PLUMBUS_RC_VAR", NULL);
    DeleteFileW(rc);
}

TEST_CASE(test_tui_config_state_navigation_toggles_and_aliases) {
    plumbus_config_t live;
    config_init(&live);

    tui_cfg_state_t st;
    tui_cfg_state_init(&st, &live);
    ASSERT_EQ_INT(TUI_ITEM_THEME, st.selected);

    /* Toggle theme forward across all THEME_COUNT themes & back */
    for (int i = 1; i < (int)THEME_COUNT; i++) {
        tui_cfg_step_selected(&st, 1);
        ASSERT_EQ_INT(i, st.draft.theme);
    }
    tui_cfg_step_selected(&st, 1);
    ASSERT_EQ_INT(THEME_ROBBYRUSSELL, st.draft.theme);
    tui_cfg_step_selected(&st, -1);
    ASSERT_EQ_INT(THEME_MINIMAL, st.draft.theme);
    tui_cfg_step_selected(&st, 1);
    tui_cfg_step_selected(&st, 1); /* agnoster */
    ASSERT_EQ_INT(THEME_AGNOSTER, st.draft.theme);

    /* Navigate down to syntax highlighting and toggle off */
    tui_cfg_move_down(&st); /* nerd_fonts */
    tui_cfg_move_down(&st); /* syntax_hl */
    ASSERT_EQ_INT(TUI_ITEM_SYNTAX_HL, st.selected);
    tui_cfg_toggle_selected(&st);
    ASSERT_TRUE(!st.draft.syntax_highlighting);

    /* Restore defaults */
    tui_cfg_restore_defaults(&st);
    ASSERT_EQ_INT(THEME_ROBBYRUSSELL, st.draft.theme);
    ASSERT_TRUE(st.draft.syntax_highlighting);

    /* Alias management */
    ASSERT_TRUE(tui_cfg_add_alias_expr(&st, L"gs=git status"));
    ASSERT_EQ_INT(1, st.draft.alias_count);
    ASSERT_TRUE(tui_cfg_remove_selected_alias(&st));
    ASSERT_EQ_INT(0, st.draft.alias_count);

    tui_cfg_state_free(&st);
    config_free(&live);
}

int wmain(void) {
    RUN_TEST(test_config_cmdrc_aliases_theme_and_expansion);
    RUN_TEST(test_tui_config_state_navigation_toggles_and_aliases);
    TEST_SUMMARY();
}
