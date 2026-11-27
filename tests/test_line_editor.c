#include "test_framework.h"
#include "line_editor.h"

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

int wmain(void) {
    RUN_TEST(test_line_buf_emacs_editing_operations);
    TEST_SUMMARY();
}
