#include "test_framework.h"
#include "history.h"
#include <windows.h>

TEST_CASE(test_history_persistence_dedup_ghost_and_reverse_search) {
    wchar_t tmp[MAX_PATH], hfile[MAX_PATH];
    GetTempPathW(MAX_PATH, tmp);
    swprintf(hfile, MAX_PATH, L"%lsplumbus_hist_test.txt", tmp);
    DeleteFileW(hfile);
    SetEnvironmentVariableW(L"PLUMBUS_HISTORY_FILE", hfile);

    history_t h;
    ASSERT_TRUE(history_init(&h, 100));
    ASSERT_TRUE(history_add(&h, L"git status"));
    ASSERT_TRUE(history_add(&h, L"git status")); /* consecutive duplicate skipped */
    ASSERT_TRUE(history_add(&h, L"dir /a"));
    ASSERT_TRUE(history_add(&h, L"git commit -m \"caf\u00e9\""));
    ASSERT_EQ_INT(3, h.len);

    const wchar_t *ghost = history_find_ghost_suffix(&h, L"git co");
    ASSERT_TRUE(ghost != NULL);
    ASSERT_EQ_WSTR(L"mmit -m \"caf\u00e9\"", ghost);

    ptrdiff_t idx1 = history_search_reverse(&h, L"GIT", -1);
    ASSERT_EQ_INT(2, idx1);
    ptrdiff_t idx2 = history_search_reverse(&h, L"GIT", idx1 - 1);
    ASSERT_EQ_INT(0, idx2);

    history_free(&h);

    /* Reload from disk */
    history_t h2;
    ASSERT_TRUE(history_init(&h2, 100));
    ASSERT_EQ_INT(3, h2.len);
    ASSERT_EQ_WSTR(L"git commit -m \"caf\u00e9\"", history_get(&h2, 2));
    history_free(&h2);

    DeleteFileW(hfile);
    SetEnvironmentVariableW(L"PLUMBUS_HISTORY_FILE", NULL);
}

int wmain(void) {
    RUN_TEST(test_history_persistence_dedup_ghost_and_reverse_search);
    TEST_SUMMARY();
}
