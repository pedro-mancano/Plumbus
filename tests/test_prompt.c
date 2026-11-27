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

    wchar_t *p1 = prompt_render(&cfg, path, 0, &git);
    ASSERT_CONTAINS_WSTR(p1, L"~\\Projects");
    ASSERT_CONTAINS_WSTR(p1, L"git:(");
    ASSERT_CONTAINS_WSTR(p1, L"main");
    ASSERT_CONTAINS_WSTR(p1, L"\u2717");

    size_t vis = prompt_visible_width(p1);
    ASSERT_TRUE(vis > 10 && vis < 60);
    free(p1);

    cfg.theme = THEME_AGNOSTER;
    cfg.nerd_fonts = false;
    wchar_t *p2 = prompt_render(&cfg, path, 2, &git);
    ASSERT_CONTAINS_WSTR(p2, L"\u2717 2");
    ASSERT_CONTAINS_WSTR(p2, L"~\\Projects");
    free(p2);

    config_free(&cfg);
}

int wmain(void) {
    RUN_TEST(test_prompt_themes_home_abbr_and_visible_width);
    TEST_SUMMARY();
}
