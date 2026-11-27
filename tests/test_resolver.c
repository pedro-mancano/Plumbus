#include "test_framework.h"
#include "resolver.h"
#include <windows.h>

TEST_CASE(test_resolver_internals_and_fastpath) {
    ASSERT_TRUE(resolver_is_cmd_internal(L"ECHO"));
    ASSERT_TRUE(resolver_is_cmd_internal(L"setlocal"));
    ASSERT_TRUE(!resolver_is_cmd_internal(L"git"));

    cmd_resolution_t res;
    ASSERT_TRUE(resolver_classify_and_find(L"cd ..", &res));
    ASSERT_EQ_INT(CMD_KIND_NATIVE_FASTPATH, res.kind);

    ASSERT_TRUE(resolver_classify_and_find(L"D:", &res));
    ASSERT_EQ_INT(CMD_KIND_NATIVE_FASTPATH, res.kind);

    ASSERT_TRUE(resolver_classify_and_find(L"dir /a", &res));
    ASSERT_EQ_INT(CMD_KIND_CMD_INTERNAL, res.kind);

    ASSERT_TRUE(resolver_classify_and_find(L"echo hello | findstr h", &res));
    ASSERT_EQ_INT(CMD_KIND_CMD_COMPOUND, res.kind);
    ASSERT_TRUE(res.has_shell_meta);
}

TEST_CASE(test_resolver_precedence_internal_cwd_path) {
    wchar_t tmp[MAX_PATH];
    GetTempPathW(MAX_PATH, tmp);
    wchar_t cwd_dir[MAX_PATH], path_dir[MAX_PATH], orig_cwd[MAX_PATH];
    GetCurrentDirectoryW(MAX_PATH, orig_cwd);

    swprintf(cwd_dir, MAX_PATH, L"%lsplumbus_res_cwd", tmp);
    swprintf(path_dir, MAX_PATH, L"%lsplumbus_res_path", tmp);
    CreateDirectoryW(cwd_dir, NULL);
    CreateDirectoryW(path_dir, NULL);

    wchar_t f_echo[MAX_PATH], f_cwd_bat[MAX_PATH], f_cwd_cmd[MAX_PATH], f_path_bat[MAX_PATH];
    swprintf(f_echo, MAX_PATH, L"%ls\\echo.bat", cwd_dir);
    swprintf(f_cwd_bat, MAX_PATH, L"%ls\\prio.bat", cwd_dir);
    swprintf(f_cwd_cmd, MAX_PATH, L"%ls\\prio.cmd", cwd_dir);
    swprintf(f_path_bat, MAX_PATH, L"%ls\\prio.bat", path_dir);

    const wchar_t *files[] = { f_echo, f_cwd_bat, f_cwd_cmd, f_path_bat };
    for (int i = 0; i < 4; i++) {
        HANDLE h = CreateFileW(files[i], GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
    }

    SetCurrentDirectoryW(cwd_dir);

    cmd_resolution_t res;
    /* Internal echo beats echo.bat in CWD */
    ASSERT_TRUE(resolver_classify_and_find(L"echo test", &res));
    ASSERT_EQ_INT(CMD_KIND_CMD_INTERNAL, res.kind);

    /* CWD .bat beats CWD .cmd */
    ASSERT_TRUE(resolver_classify_and_find(L"prio", &res));
    ASSERT_EQ_INT(CMD_KIND_BATCH_SCRIPT, res.kind);
    ASSERT_TRUE(_wcsicmp(f_cwd_bat, res.resolved_path) == 0);

    /* Cleanup */
    SetCurrentDirectoryW(orig_cwd);
    for (int i = 0; i < 4; i++) DeleteFileW(files[i]);
    RemoveDirectoryW(cwd_dir);
    RemoveDirectoryW(path_dir);
}

int wmain(void) {
    RUN_TEST(test_resolver_internals_and_fastpath);
    RUN_TEST(test_resolver_precedence_internal_cwd_path);
    TEST_SUMMARY();
}
