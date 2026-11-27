#include "test_framework.h"
#include "state_sync.h"
#include <windows.h>

TEST_CASE(test_state_sync_roundtrip_unicode_and_diff) {
    wchar_t tmp[MAX_PATH], orig_cwd[MAX_PATH], sub[MAX_PATH], sync_file[MAX_PATH];
    GetTempPathW(MAX_PATH, tmp);
    GetCurrentDirectoryW(MAX_PATH, orig_cwd);

    swprintf(sub, MAX_PATH, L"%lsplumbus_sync_caf\u00e9", tmp);
    CreateDirectoryW(sub, NULL);
    swprintf(sync_file, MAX_PATH, L"%lsplumbus_sync_test.bin", tmp);

    SetEnvironmentVariableW(L"PLUMBUS_TEMP_KEEP", L"initial");
    SetEnvironmentVariableW(L"PLUMBUS_TO_DELETE", L"bye");

    /* Simulate child state: change CWD, update/add/delete vars */
    SetCurrentDirectoryW(sub);
    SetEnvironmentVariableW(L"PLUMBUS_TEMP_KEEP", L"updated_a\u00e7\u00e3o");
    SetEnvironmentVariableW(L"PLUMBUS_TO_DELETE", NULL);
    SetEnvironmentVariableW(L"ERRORLEVEL", NULL);

    ASSERT_EQ_INT(42, state_sync_write_current(sync_file, 42));

    /* Restore parent state before applying */
    SetCurrentDirectoryW(orig_cwd);
    SetEnvironmentVariableW(L"PLUMBUS_TEMP_KEEP", L"initial");
    SetEnvironmentVariableW(L"PLUMBUS_TO_DELETE", L"bye");

    synced_state_t st;
    ASSERT_TRUE(state_sync_read_file(sync_file, &st));
    ASSERT_EQ_INT(42, st.exit_code);
    /* File should be deleted after read */
    ASSERT_TRUE(GetFileAttributesW(sync_file) == INVALID_FILE_ATTRIBUTES);

    ASSERT_TRUE(state_sync_apply(&st));
    state_sync_free(&st);

    wchar_t cur_cwd[MAX_PATH];
    GetCurrentDirectoryW(MAX_PATH, cur_cwd);
    ASSERT_EQ_WSTR(sub, cur_cwd);

    wchar_t val[128];
    DWORD n = GetEnvironmentVariableW(L"PLUMBUS_TEMP_KEEP", val, 128);
    ASSERT_TRUE(n > 0);
    ASSERT_EQ_WSTR(L"updated_a\u00e7\u00e3o", val);

    /* Deleted variable must be gone */
    ASSERT_EQ_INT(0, GetEnvironmentVariableW(L"PLUMBUS_TO_DELETE", val, 128));
    /* Literal ERRORLEVEL must NOT be injected */
    ASSERT_EQ_INT(0, GetEnvironmentVariableW(L"ERRORLEVEL", val, 128));

    /* Restore */
    SetCurrentDirectoryW(orig_cwd);
    SetEnvironmentVariableW(L"PLUMBUS_TEMP_KEEP", NULL);
    RemoveDirectoryW(sub);
}

int wmain(void) {
    RUN_TEST(test_state_sync_roundtrip_unicode_and_diff);
    TEST_SUMMARY();
}
