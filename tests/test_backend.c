#include "test_framework.h"
#include "backend.h"
#include <windows.h>

TEST_CASE(test_backend_same_file_detection) {
    wchar_t self[MAX_PATH * 2];
    GetModuleFileNameW(NULL, self, MAX_PATH * 2);
    ASSERT_TRUE(backend_is_same_file(self, self));

    wchar_t sys_cmd[MAX_PATH];
    GetSystemDirectoryW(sys_cmd, MAX_PATH);
    wcscat(sys_cmd, L"\\cmd.exe");
    ASSERT_TRUE(!backend_is_same_file(self, sys_cmd));
}

TEST_CASE(test_backend_self_loop_rejected) {
    wchar_t self[MAX_PATH * 2];
    GetModuleFileNameW(NULL, self, MAX_PATH * 2);
    SetEnvironmentVariableW(L"PLUMBUS_CMD_BACKEND", self);

    wchar_t out[MAX_PATH * 2];
    backend_status_t st = backend_resolve_cmd(out, MAX_PATH * 2);
    SetEnvironmentVariableW(L"PLUMBUS_CMD_BACKEND", NULL);

    ASSERT_EQ_INT(BACKEND_ERR_SELF_LOOP, st);
}

TEST_CASE(test_backend_resolves_backup_or_system) {
    SetEnvironmentVariableW(L"PLUMBUS_CMD_BACKEND", NULL);
    wchar_t out[MAX_PATH * 2];
    backend_status_t st = backend_resolve_cmd(out, MAX_PATH * 2);
    ASSERT_EQ_INT(BACKEND_OK, st);
    ASSERT_TRUE(GetFileAttributesW(out) != INVALID_FILE_ATTRIBUTES);
}

int wmain(void) {
    RUN_TEST(test_backend_same_file_detection);
    RUN_TEST(test_backend_self_loop_rejected);
    RUN_TEST(test_backend_resolves_backup_or_system);
    TEST_SUMMARY();
}
