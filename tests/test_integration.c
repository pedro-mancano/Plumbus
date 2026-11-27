#include "test_framework.h"
#include "exec.h"
#include "backend.h"
#include <windows.h>
#include <stdio.h>
#include <string.h>

static void ensure_dir(const wchar_t *dir) {
    CreateDirectoryW(dir, NULL);
}

static void write_ascii_file(const wchar_t *path, const char *content) {
    HANDLE h = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h != INVALID_HANDLE_VALUE) {
        DWORD w = 0;
        WriteFile(h, content, (DWORD)strlen(content), &w, NULL);
        CloseHandle(h);
    }
}

static int run_plumbus_captured(const wchar_t *raw_args, const wchar_t *stdin_data, const wchar_t *work_dir,
                                char *out_buf, size_t out_cap) {
    SECURITY_ATTRIBUTES sa = { sizeof(sa), NULL, TRUE };
    HANDLE out_r, out_w, in_r, in_w;
    CreatePipe(&out_r, &out_w, &sa, 0);
    CreatePipe(&in_r, &in_w, &sa, 0);
    SetHandleInformation(out_r, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(in_w, HANDLE_FLAG_INHERIT, 0);

    if (stdin_data && *stdin_data) {
        char utf8[4096];
        int n = WideCharToMultiByte(CP_UTF8, 0, stdin_data, -1, utf8, sizeof(utf8), NULL, NULL);
        if (n > 1) {
            DWORD w = 0;
            WriteFile(in_w, utf8, (DWORD)(n - 1), &w, NULL);
        }
    }
    CloseHandle(in_w);

    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof(si));
    ZeroMemory(&pi, sizeof(pi));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = in_r;
    si.hStdOutput = out_w;
    si.hStdError = out_w;

    wchar_t cmdline[8192];
    swprintf(cmdline, 8192, L"\"%ls\" %ls", PLUMBUS_EXE_PATH, raw_args ? raw_args : L"");

    BOOL ok = CreateProcessW(PLUMBUS_EXE_PATH, cmdline, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, work_dir, &si, &pi);
    CloseHandle(out_w);
    CloseHandle(in_r);

    if (!ok) {
        CloseHandle(out_r);
        return -1;
    }

    size_t total = 0;
    DWORD rd = 0;
    while (total + 1 < out_cap && ReadFile(out_r, out_buf + total, (DWORD)(out_cap - 1 - total), &rd, NULL) && rd > 0) {
        total += rd;
    }
    out_buf[total] = '\0';
    CloseHandle(out_r);

    WaitForSingleObject(pi.hProcess, 10000);
    DWORD ec = 0;
    GetExitCodeProcess(pi.hProcess, &ec);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return (int)ec;
}

TEST_CASE(test_integ_proxy_c_exitcode_and_quotes) {
    ensure_dir(PLUMBUS_TEST_SANDBOX_DIR);
    wchar_t space_dir[MAX_PATH];
    swprintf(space_dir, MAX_PATH, L"%ls\\dir com espaco", PLUMBUS_TEST_SANDBOX_DIR);
    ensure_dir(space_dir);

    wchar_t bat[MAX_PATH];
    swprintf(bat, MAX_PATH, L"%ls\\hello.bat", space_dir);
    write_ascii_file(bat, "@echo RAN_BAT arg=[%1]\r\n@exit /b 42\r\n");

    char out[4096];
    int ec = run_plumbus_captured(L"/d /c \"\"dir com espaco\\hello.bat\" \"arg 1\"\"", NULL, PLUMBUS_TEST_SANDBOX_DIR, out, sizeof(out));
    ASSERT_EQ_INT(42, ec);
    ASSERT_TRUE(strstr(out, "RAN_BAT arg=[\"arg 1\"]") != NULL);
}

TEST_CASE(test_integ_piped_stdin_delegation) {
    char out[4096];
    int ec = run_plumbus_captured(L"/d /q", L"set PIPE_VAR=ok_pipe\r\necho PIPE=%PIPE_VAR%\r\nexit 7\r\n",
                                  PLUMBUS_TEST_SANDBOX_DIR, out, sizeof(out));
    ASSERT_EQ_INT(7, ec);
    ASSERT_TRUE(strstr(out, "PIPE=ok_pipe") != NULL);
}

TEST_CASE(test_integ_comspec_no_infinite_loop) {
    SetEnvironmentVariableW(L"ComSpec", PLUMBUS_EXE_PATH);
    char out[4096];
    int ec = run_plumbus_captured(L"/d /c \"%ComSpec% /d /c echo COMSPEC_OK\"", NULL, PLUMBUS_TEST_SANDBOX_DIR, out, sizeof(out));
    SetEnvironmentVariableW(L"ComSpec", NULL);
    ASSERT_EQ_INT(0, ec);
    ASSERT_TRUE(strstr(out, "COMSPEC_OK") != NULL);
}

TEST_CASE(test_integ_standalone_zero_cmd_dependency) {
    SetEnvironmentVariableW(L"PLUMBUS_CMD_BACKEND", L"C:\\nonexistent_dir_xyz\\cmd.exe");
    SetEnvironmentVariableW(L"ComSpec", PLUMBUS_EXE_PATH);
    char out[4096];
    int ec = run_plumbus_captured(L"/d /c \"echo STANDALONE_OK & set /a X=2+3 & call echo X=%X%\"",
                                  NULL, PLUMBUS_TEST_SANDBOX_DIR, out, sizeof(out));
    SetEnvironmentVariableW(L"ComSpec", NULL);
    SetEnvironmentVariableW(L"PLUMBUS_CMD_BACKEND", NULL);
    ASSERT_EQ_INT(0, ec);
    ASSERT_TRUE(strstr(out, "STANDALONE_OK") != NULL);
    ASSERT_TRUE(strstr(out, "X=5") != NULL);
}

TEST_CASE(test_integ_interactive_persistence_vcvars_style) {
    wchar_t sandbox[MAX_PATH];
    wcsncpy(sandbox, PLUMBUS_TEST_SANDBOX_DIR, MAX_PATH - 1);
    sandbox[MAX_PATH - 1] = L'\0';
    for (wchar_t *s = sandbox; *s; s++) {
        if (*s == L'/') *s = L'\\';
    }

    wchar_t orig_cwd[MAX_PATH];
    GetCurrentDirectoryW(MAX_PATH, orig_cwd);
    ensure_dir(sandbox);

    wchar_t sub[MAX_PATH];
    swprintf(sub, MAX_PATH, L"%ls\\subpasta_caf\u00e9", sandbox);
    ensure_dir(sub);

    wchar_t vcvars[MAX_PATH];
    swprintf(vcvars, MAX_PATH, L"%ls\\fake_vcvarsall.bat", sandbox);
    write_ascii_file(vcvars,
        "@echo off\r\n"
        "set VCINSTALLDIR=C:\\FakeVS\\VC\\\r\n"
        "set PATH=C:\\FakeVS\\bin;%PATH%\r\n"
        "cd /d \"%~dp0subpasta_caf\xC3\xA9\"\r\n"
        "exit /b 0\r\n");

    wchar_t backend[MAX_PATH * 2];
    ASSERT_EQ_INT(BACKEND_OK, backend_resolve_cmd(backend, MAX_PATH * 2));

    shell_ctx_t ctx;
    ASSERT_TRUE(shell_ctx_init(&ctx, NULL, backend));
    /* Point ctx.self_path to built plumbus.exe so Sync Bridge invokes plumbus.exe --__sync-state */
    wcsncpy(ctx.self_path, PLUMBUS_EXE_PATH, (sizeof(ctx.self_path) / sizeof(ctx.self_path[0])) - 1);

    SetCurrentDirectoryW(sandbox);

    /* 1. Fast-path cd / pushd / popd */
    ASSERT_EQ_INT(0, exec_interactive_line(&ctx, L"pushd subpasta_caf\u00e9"));
    wchar_t cwd[MAX_PATH];
    GetCurrentDirectoryW(MAX_PATH, cwd);
    ASSERT_EQ_WSTR(sub, cwd);

    ASSERT_EQ_INT(0, exec_interactive_line(&ctx, L"popd"));
    GetCurrentDirectoryW(MAX_PATH, cwd);
    ASSERT_EQ_WSTR(sandbox, cwd);

    /* 2. Internal set & unset via Sync Bridge */
    ASSERT_EQ_INT(0, exec_interactive_line(&ctx, L"set PLUMBUS_INTERACTIVE_VAR=hello_world"));
    wchar_t val[512];
    ASSERT_TRUE(GetEnvironmentVariableW(L"PLUMBUS_INTERACTIVE_VAR", val, 512) > 0);
    ASSERT_EQ_WSTR(L"hello_world", val);

    ASSERT_EQ_INT(0, exec_interactive_line(&ctx, L"set PLUMBUS_INTERACTIVE_VAR="));
    ASSERT_EQ_INT(0, GetEnvironmentVariableW(L"PLUMBUS_INTERACTIVE_VAR", val, 512));

    /* 3. Bang (!) literal preservation + ERRORLEVEL capture */
    ASSERT_EQ_INT(0, exec_interactive_line(&ctx, L"echo a!PATH!b > nul"));
    ASSERT_EQ_INT(0, ctx.last_exit_code);

    /* 4. fake_vcvarsall.bat persists ENV + CWD + ExitCode */
    ASSERT_EQ_INT(0, exec_interactive_line(&ctx, L"fake_vcvarsall.bat"));
    ASSERT_TRUE(GetEnvironmentVariableW(L"VCINSTALLDIR", val, 512) > 0);
    ASSERT_EQ_WSTR(L"C:\\FakeVS\\VC\\", val);
    GetCurrentDirectoryW(MAX_PATH, cwd);
    ASSERT_EQ_WSTR(sub, cwd);
    ASSERT_EQ_INT(0, GetEnvironmentVariableW(L"ERRORLEVEL", val, 512));

    /* 5. Direct .exe execution */
    wchar_t cmd_direct[MAX_PATH + 64];
    swprintf(cmd_direct, MAX_PATH + 64, L"\"%ls\" --__bench-init", PLUMBUS_EXE_PATH);
    ASSERT_EQ_INT(0, exec_interactive_line(&ctx, cmd_direct));

    /* 6. Nonexistent command sets 9009 and subsequent echo %ERRORLEVEL% sees 9009 */
    ASSERT_EQ_INT(9009, exec_interactive_line(&ctx, L"nao_existe_xyz_999 2>nul"));
    ASSERT_EQ_INT(9009, ctx.last_exit_code);
    ASSERT_EQ_INT(0, exec_interactive_line(&ctx, L"echo %ERRORLEVEL% > ec_out.txt"));
    FILE *fec = _wfopen(L"ec_out.txt", L"r");
    ASSERT_TRUE(fec != NULL);
    char ecbuf[64] = { 0 };
    if (fec) { fread(ecbuf, 1, sizeof(ecbuf) - 1, fec); fclose(fec); }
    ASSERT_TRUE(strstr(ecbuf, "9009") != NULL);
    DeleteFileW(L"ec_out.txt");

    shell_ctx_free(&ctx);
    SetCurrentDirectoryW(orig_cwd);
}

int wmain(void) {
    RUN_TEST(test_integ_proxy_c_exitcode_and_quotes);
    RUN_TEST(test_integ_piped_stdin_delegation);
    RUN_TEST(test_integ_comspec_no_infinite_loop);
    RUN_TEST(test_integ_standalone_zero_cmd_dependency);
    RUN_TEST(test_integ_interactive_persistence_vcvars_style);
    TEST_SUMMARY();
}
