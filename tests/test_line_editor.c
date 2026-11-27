#include "test_framework.h"
#include "line_editor.h"
#include <windows.h>
#include <stdlib.h>

TEST_CASE(test_line_buf_emacs_editing_operations) {
    line_buf_t lb;
    line_buf_init(&lb);

    ASSERT_TRUE(line_buf_set(&lb, L"git commit -m msg"));
    ASSERT_EQ_INT(17, lb.cursor);

    /* Ctrl+A -> home, Ctrl+E -> end */
    line_buf_move_home(&lb);
    ASSERT_EQ_INT(0, lb.cursor);
    line_buf_move_end(&lb);
    ASSERT_EQ_INT(17, lb.cursor);

    /* Ctrl+W kills "msg" */
    line_buf_kill_word_back(&lb);
    ASSERT_EQ_WSTR(L"git commit -m ", lb.buf);

    /* Ctrl+Left jumps back over "m " (past '-' separator) */
    line_buf_move_word_left(&lb);
    ASSERT_EQ_INT(12, lb.cursor);

    /* Ctrl+K kills to end */
    line_buf_kill_to_end(&lb);
    ASSERT_EQ_WSTR(L"git commit -", lb.buf);

    /* Ctrl+Left skips '-' and "commit" -> index 4 */
    line_buf_move_word_left(&lb);
    line_buf_kill_to_start(&lb);
    ASSERT_EQ_WSTR(L"commit -", lb.buf);
    ASSERT_EQ_INT(0, lb.cursor);

    /* Insert string */
    line_buf_insert_str(&lb, L"git ");
    ASSERT_EQ_WSTR(L"git commit -", lb.buf);
    ASSERT_EQ_INT(4, lb.cursor);

    line_buf_free(&lb);
}

TEST_CASE(test_line_editor_config_shortcuts) {
    ASSERT_TRUE(line_editor_is_config_shortcut(VK_F2, 0, 0));
    ASSERT_TRUE(line_editor_is_config_shortcut(VK_OEM_COMMA, 0, LEFT_CTRL_PRESSED));
    ASSERT_TRUE(line_editor_is_config_shortcut(0, L',', RIGHT_CTRL_PRESSED));
    ASSERT_TRUE(!line_editor_is_config_shortcut(VK_OEM_COMMA, L',', 0));
    ASSERT_TRUE(!line_editor_is_config_shortcut('A', L'a', LEFT_CTRL_PRESSED));
}

TEST_CASE(test_line_editor_ghost_text_cleared_on_commit) {
    line_buf_t lb;
    line_buf_init(&lb);
    line_buf_set(&lb, L"echo hel");

    wchar_t *with_ghost = line_editor_format_frame(L"> ", 2, &lb, L"lo_tail", true, NULL, NULL);
    ASSERT_TRUE(with_ghost != NULL);
    ASSERT_CONTAINS_WSTR(with_ghost, L"\x1b[38;2;110;110;110mlo_tail");
    free(with_ghost);

    /* On Enter commit, ghost is passed as NULL -> no gray suffix, \x1b[K clears remainder */
    wchar_t *committed = line_editor_format_frame(L"> ", 2, &lb, NULL, true, NULL, NULL);
    ASSERT_TRUE(committed != NULL);
    ASSERT_TRUE(wcsstr(committed, L"lo_tail") == NULL);
    ASSERT_CONTAINS_WSTR(committed, L"\x1b[K");
    free(committed);

    line_buf_free(&lb);
}

int wmain(void) {
    RUN_TEST(test_line_buf_emacs_editing_operations);
    RUN_TEST(test_line_editor_config_shortcuts);
    RUN_TEST(test_line_editor_ghost_text_cleared_on_commit);
    TEST_SUMMARY();
}
