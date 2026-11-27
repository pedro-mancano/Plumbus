#include "test_framework.h"
#include "completion.h"
#include <windows.h>

TEST_CASE(test_completion_env_git_and_files) {
    /* 1. %VAR% completion */
    SetEnvironmentVariableW(L"PLUMBUS_COMP_UNIQUE_VAR", L"1");
    completion_list_t c1;
    completion_generate(L"echo %PLUMBUS_COMP_UNI", 22, &c1);
    ASSERT_TRUE(c1.count >= 1);
    ASSERT_EQ_WSTR(L"%PLUMBUS_COMP_UNIQUE_VAR%", c1.items[0]);
    completion_free(&c1);
    SetEnvironmentVariableW(L"PLUMBUS_COMP_UNIQUE_VAR", NULL);

    /* 2. Git subcommand completion */
    completion_list_t c2;
    completion_generate(L"git che", 7, &c2);
    ASSERT_TRUE(c2.count >= 2); /* checkout, cherry-pick */
    ASSERT_EQ_WSTR(L"checkout", c2.items[0]);
    completion_free(&c2);

    /* 3. File & directory completion */
    wchar_t tmp[MAX_PATH], orig[MAX_PATH], dir[MAX_PATH], sub[MAX_PATH], file_space[MAX_PATH];
    GetTempPathW(MAX_PATH, tmp);
    GetCurrentDirectoryW(MAX_PATH, orig);
    swprintf(dir, MAX_PATH, L"%lsplumbus_comp_dir", tmp);
    CreateDirectoryW(dir, NULL);
    swprintf(sub, MAX_PATH, L"%ls\\alpha_dir", dir);
    CreateDirectoryW(sub, NULL);
    swprintf(file_space, MAX_PATH, L"%ls\\alpha file.txt", dir);
    HANDLE hf = CreateFileW(file_space, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hf != INVALID_HANDLE_VALUE) CloseHandle(hf);

    SetCurrentDirectoryW(dir);
    completion_list_t c3;
    completion_generate(L"type alpha", 10, &c3);
    ASSERT_EQ_INT(2, c3.count);
    ASSERT_EQ_WSTR(L"\"alpha file.txt\"", c3.items[0]);
    ASSERT_EQ_WSTR(L"alpha_dir\\", c3.items[1]);
    completion_free(&c3);

    SetCurrentDirectoryW(orig);
    DeleteFileW(file_space);
    RemoveDirectoryW(sub);
    RemoveDirectoryW(dir);
}

int wmain(void) {
    RUN_TEST(test_completion_env_git_and_files);
    TEST_SUMMARY();
}
