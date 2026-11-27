#include "test_framework.h"
#include "highlight.h"
#include <stdlib.h>

static bool mock_alias(const wchar_t *name, void *ud) {
    (void)ud;
    return _wcsicmp(name, L"ll") == 0;
}

TEST_CASE(test_highlight_colors_valid_invalid_flags_strings_ghost) {
    /* Valid internal cmd -> green, flag -> cyan, string -> yellow, ghost -> gray */
    wchar_t *s1 = highlight_render_line(L"dir /a \"caf\u00e9\"", L" --more", NULL, NULL);
    ASSERT_CONTAINS_WSTR(s1, L"\x1b[38;2;80;250;123mdir");
    ASSERT_CONTAINS_WSTR(s1, L"\x1b[38;2;139;233;253m/a");
    ASSERT_CONTAINS_WSTR(s1, L"\x1b[38;2;241;250;140m\"caf\u00e9\"");
    ASSERT_CONTAINS_WSTR(s1, L"\x1b[38;2;110;110;110m --more");
    free(s1);

    /* Invalid command -> red; alias -> green */
    wchar_t *s2 = highlight_render_line(L"no_such_cmd_xyz | ll", NULL, mock_alias, NULL);
    ASSERT_CONTAINS_WSTR(s2, L"\x1b[38;2;255;85;85mno_such_cmd_xyz");
    ASSERT_CONTAINS_WSTR(s2, L"\x1b[38;2;80;250;123mll");
    free(s2);
}

int wmain(void) {
    RUN_TEST(test_highlight_colors_valid_invalid_flags_strings_ghost);
    TEST_SUMMARY();
}
