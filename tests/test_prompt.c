#include "test_framework.h"
#include "prompt.h"
#include <windows.h>
#include <stdlib.h>

TEST_CASE(test_prompt_themes_home_abbr_and_visible_width) {
    plumbus_config_t cfg;
    config_init(&cfg);

    wchar_t home[MAX_PATH];
    GetEnvironmentVariableW(L"USERPROFILE", home, MAX_PATH);
    wchar_t path[MAX_PATH + 32];
    swprintf(path, MAX_PATH + 32, L"%ls\\Projects", home);

    git_status_t git = { 0 };
    git.in_repo = true;
    git.is_dirty = true;
    wcscpy(git.branch, L"main");

    /* 0. cmd (default, identical to stock cmd.exe: full path + '>', no ANSI, no Git) */
    ASSERT_EQ_INT(THEME_CMD, cfg.theme);
    SetEnvironmentVariableW(L"PROMPT", NULL);
    wchar_t *p0 = prompt_render(&cfg, path, 1, &git);
    wchar_t expected_cmd[MAX_PATH + 40];
    swprintf(expected_cmd, MAX_PATH + 40, L"%ls>", path);
    ASSERT_EQ_WSTR(expected_cmd, p0);
    ASSERT_TRUE(wcschr(p0, L'\x1b') == NULL);
    ASSERT_TRUE(wcsstr(p0, L"main") == NULL);
    ASSERT_EQ_INT((int)(wcslen(path) + 1), (int)prompt_visible_width(p0));
    free(p0);

    SetEnvironmentVariableW(L"PROMPT", L"[$P]$S$G");
    wchar_t *p0_custom = prompt_render(&cfg, path, 0, &git);
    swprintf(expected_cmd, MAX_PATH + 40, L"[%ls] >", path);
    ASSERT_EQ_WSTR(expected_cmd, p0_custom);
    free(p0_custom);
    SetEnvironmentVariableW(L"PROMPT", NULL);

    /* 1. robbyrussell */
    cfg.theme = THEME_ROBBYRUSSELL;
    wchar_t *p1 = prompt_render(&cfg, path, 0, &git);
    ASSERT_CONTAINS_WSTR(p1, L"~\\Projects");
    ASSERT_CONTAINS_WSTR(p1, L"git:(");
    ASSERT_CONTAINS_WSTR(p1, L"main");
    ASSERT_CONTAINS_WSTR(p1, L"\u2717");
    size_t vis = prompt_visible_width(p1);
    ASSERT_TRUE(vis > 10 && vis < 60);
    free(p1);

    /* 2. agnoster */
    cfg.theme = THEME_AGNOSTER;
    cfg.nerd_fonts = false;
    wchar_t *p2 = prompt_render(&cfg, path, 2, &git);
    ASSERT_CONTAINS_WSTR(p2, L"\u2717 2");
    ASSERT_CONTAINS_WSTR(p2, L"~\\Projects");
    free(p2);

    /* 3. omb-box (two-line Oh My Bash style with native Git) */
    cfg.theme = THEME_OMB_BOX;
    git.is_dirty = false;
    wchar_t *p3 = prompt_render(&cfg, path, 0, &git);
    ASSERT_CONTAINS_WSTR(p3, L"\u250F\u2501\u2501"); /* ┏━━ */
    ASSERT_CONTAINS_WSTR(p3, L" at ");
    ASSERT_CONTAINS_WSTR(p3, L" in ");
    ASSERT_CONTAINS_WSTR(p3, L"~\\Projects");
    ASSERT_CONTAINS_WSTR(p3, L" on ");
    ASSERT_CONTAINS_WSTR(p3, L"main ");
    ASSERT_CONTAINS_WSTR(p3, L"\u2714");
    ASSERT_CONTAINS_WSTR(p3, L"\n");
    ASSERT_CONTAINS_WSTR(p3, L"\u2517\u2501"); /* ┗━ */
    ASSERT_CONTAINS_WSTR(p3, L"$ ");
    ASSERT_EQ_INT(4, prompt_last_line_visible_width(p3)); /* "┗━$ " = 4 cols */
    free(p3);

    /* 4. pure, dracula, minimal */
    cfg.theme = THEME_PURE;
    cfg.nerd_fonts = true;
    wchar_t *p4 = prompt_render(&cfg, path, 0, &git);
    ASSERT_CONTAINS_WSTR(p4, L"~\\Projects");
    ASSERT_CONTAINS_WSTR(p4, L"\n");
    ASSERT_CONTAINS_WSTR(p4, L"\u276F ");
    free(p4);

    cfg.theme = THEME_DRACULA;
    wchar_t *p5 = prompt_render(&cfg, path, 0, &git);
    ASSERT_CONTAINS_WSTR(p5, L"\u256D\u2500"); /* ╭─ */
    ASSERT_CONTAINS_WSTR(p5, L"\u2570\u2500"); /* ╰─ */
    free(p5);

    cfg.theme = THEME_MINIMAL;
    wchar_t *p6 = prompt_render(&cfg, path, 0, &git);
    ASSERT_CONTAINS_WSTR(p6, L"~\\Projects");
    ASSERT_CONTAINS_WSTR(p6, L"[");
    ASSERT_CONTAINS_WSTR(p6, L"main ");
    ASSERT_CONTAINS_WSTR(p6, L"\u2714");
    free(p6);

    config_free(&cfg);
}

int wmain(void) {
    RUN_TEST(test_prompt_themes_home_abbr_and_visible_width);
    TEST_SUMMARY();
}
