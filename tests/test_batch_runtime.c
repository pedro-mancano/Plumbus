#include "test_framework.h"
#include "exec.h"
#include "cmd_runtime.h"
#include <windows.h>
#include <stdio.h>
#include <string.h>

static int run_cap(shell_ctx_t *ctx, const wchar_t *line, char *out, size_t cap) {
    SECURITY_ATTRIBUTES sa = { sizeof(sa), NULL, TRUE };
    HANDLE r = NULL, w = NULL;
    CreatePipe(&r, &w, &sa, 0);
    SetHandleInformation(r, HANDLE_FLAG_INHERIT, 0);
    cmd_io_t io = { GetStdHandle(STD_INPUT_HANDLE), w, w };
    int rc = cmd_runtime_exec_line(ctx, line, &io, true);
    CloseHandle(w);
    size_t tot = 0;
    DWORD rd = 0;
    while (tot + 1 < cap && ReadFile(r, out + tot, (DWORD)(cap - 1 - tot), &rd, NULL) && rd > 0) {
        tot += rd;
    }
    out[tot] = '\0';
    CloseHandle(r);
    return rc;
}

TEST_CASE(test_runtime_pipes_and_if_for) {
    shell_ctx_t ctx;
    ASSERT_TRUE(shell_ctx_init(&ctx, NULL, NULL));
    char out[4096];

    /* Native pipe to findstr.exe */
    ASSERT_EQ_INT(0, run_cap(&ctx, L"echo alpha_pipe_test | findstr alpha_pipe_test", out, sizeof(out)));
    ASSERT_TRUE(strstr(out, "alpha_pipe_test") != NULL);

    /* FOR /L + IF */
    ASSERT_EQ_INT(0, run_cap(&ctx, L"for /L %i in (1,1,3) do @if %i equ 2 (echo HIT_%i) else (echo MISS_%i)", out, sizeof(out)));
    ASSERT_TRUE(strstr(out, "MISS_1") != NULL);
    ASSERT_TRUE(strstr(out, "HIT_2") != NULL);
    ASSERT_TRUE(strstr(out, "MISS_3") != NULL);

    /* FOR /F string + tokens */
    ASSERT_EQ_INT(0, run_cap(&ctx, L"for /f \"tokens=1,2 delims=,\" %a in (\"one,two\") do @echo %a-%b", out, sizeof(out)));
    ASSERT_TRUE(strstr(out, "one-two") != NULL);

    /* FOR /F command */
    ASSERT_EQ_INT(0, run_cap(&ctx, L"for /f %x in ('echo CMD_OUT_OK') do @echo GOT=%x", out, sizeof(out)));
    ASSERT_TRUE(strstr(out, "GOT=CMD_OUT_OK") != NULL);

    shell_ctx_free(&ctx);
}

TEST_CASE(test_runtime_batch_script_full) {
    shell_ctx_t ctx;
    ASSERT_TRUE(shell_ctx_init(&ctx, NULL, NULL));

    wchar_t tmp[MAX_PATH];
    GetTempPathW(MAX_PATH, tmp);
    wchar_t bat[MAX_PATH];
    swprintf(bat, MAX_PATH, L"%lsplumbus_test_%lu.bat", tmp, (unsigned long)GetCurrentProcessId());

    HANDLE f = CreateFileW(bat, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    ASSERT_TRUE(f != INVALID_HANDLE_VALUE);
    const char *script =
        "@echo off\r\n"
        "setlocal enabledelayedexpansion\r\n"
        "set ACC=0\r\n"
        "for /l %%i in (1,1,3) do (\r\n"
        "  set /a ACC+=%%i\r\n"
        ")\r\n"
        "echo SUM=!ACC!\r\n"
        "call :sub first second\r\n"
        "goto :done\r\n"
        "echo SHOULD_NOT_RUN\r\n"
        ":sub\r\n"
        "echo SUB1=%1\r\n"
        "shift\r\n"
        "echo SUB2=%1\r\n"
        "exit /b 0\r\n"
        ":done\r\n"
        "echo DP0=%~nx0\r\n"
        "endlocal\r\n"
        "exit /b 7\r\n";
    DWORD wr = 0;
    WriteFile(f, script, (DWORD)strlen(script), &wr, NULL);
    CloseHandle(f);

    char out[4096];
    wchar_t cmd[MAX_PATH + 16];
    swprintf(cmd, MAX_PATH + 16, L"\"%ls\"", bat);
    int rc = run_cap(&ctx, cmd, out, sizeof(out));
    DeleteFileW(bat);

    ASSERT_EQ_INT(7, rc);
    ASSERT_TRUE(strstr(out, "SUM=6") != NULL);
    ASSERT_TRUE(strstr(out, "SUB1=first") != NULL);
    ASSERT_TRUE(strstr(out, "SUB2=second") != NULL);
    ASSERT_TRUE(strstr(out, "SHOULD_NOT_RUN") == NULL);
    ASSERT_TRUE(strstr(out, ".bat") != NULL);

    shell_ctx_free(&ctx);
}

int wmain(void) {
    RUN_TEST(test_runtime_pipes_and_if_for);
    RUN_TEST(test_runtime_batch_script_full);
    TEST_SUMMARY();
}
