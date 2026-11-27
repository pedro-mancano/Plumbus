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

TEST_CASE(test_builtins_set_arith_and_setlocal) {
    shell_ctx_t ctx;
    ASSERT_TRUE(shell_ctx_init(&ctx, NULL, NULL));
    char out[2048];

    ASSERT_EQ_INT(0, run_cap(&ctx, L"set /a \"A=2+3*4, B=(1<<4)|3\"", out, sizeof(out)));
    wchar_t val[64];
    ASSERT_TRUE(GetEnvironmentVariableW(L"A", val, 64) > 0);
    ASSERT_EQ_WSTR(L"14", val);
    ASSERT_TRUE(GetEnvironmentVariableW(L"B", val, 64) > 0);
    ASSERT_EQ_WSTR(L"19", val);

    ASSERT_EQ_INT(0, run_cap(&ctx, L"setlocal enabledelayedexpansion", out, sizeof(out)));
    ASSERT_TRUE(ctx.delayed_expansion);
    ASSERT_EQ_INT(0, run_cap(&ctx, L"set A=999", out, sizeof(out)));
    ASSERT_EQ_INT(0, run_cap(&ctx, L"endlocal", out, sizeof(out)));
    ASSERT_TRUE(!ctx.delayed_expansion);
    ASSERT_TRUE(GetEnvironmentVariableW(L"A", val, 64) > 0);
    ASSERT_EQ_WSTR(L"14", val);

    SetEnvironmentVariableW(L"A", NULL);
    SetEnvironmentVariableW(L"B", NULL);
    shell_ctx_free(&ctx);
}

TEST_CASE(test_builtins_fs_ops) {
    shell_ctx_t ctx;
    ASSERT_TRUE(shell_ctx_init(&ctx, NULL, NULL));
    wchar_t orig_cwd[MAX_PATH];
    GetCurrentDirectoryW(MAX_PATH, orig_cwd);

    wchar_t tmp[MAX_PATH];
    GetTempPathW(MAX_PATH, tmp);
    wchar_t root[MAX_PATH];
    swprintf(root, MAX_PATH, L"%lsplumbus_bi_test_%lu", tmp, (unsigned long)GetCurrentProcessId());
    CreateDirectoryW(root, NULL);
    SetCurrentDirectoryW(root);

    char out[4096];
    ASSERT_EQ_INT(0, run_cap(&ctx, L"mkdir sub1\\sub2", out, sizeof(out)));
    ASSERT_EQ_INT(0, run_cap(&ctx, L"echo hello>f1.txt", out, sizeof(out)));
    ASSERT_EQ_INT(0, run_cap(&ctx, L"echo world>f2.txt", out, sizeof(out)));
    ASSERT_EQ_INT(0, run_cap(&ctx, L"copy f1.txt+f2.txt f3.txt", out, sizeof(out)));
    ASSERT_EQ_INT(0, run_cap(&ctx, L"type f3.txt", out, sizeof(out)));
    ASSERT_TRUE(strstr(out, "hello") != NULL);
    ASSERT_TRUE(strstr(out, "world") != NULL);

    ASSERT_EQ_INT(0, run_cap(&ctx, L"ren f3.txt f3_ren.txt", out, sizeof(out)));
    ASSERT_EQ_INT(0, run_cap(&ctx, L"move f3_ren.txt sub1\\sub2", out, sizeof(out)));
    ASSERT_EQ_INT(0, run_cap(&ctx, L"dir /b /s sub1", out, sizeof(out)));
    ASSERT_TRUE(strstr(out, "f3_ren.txt") != NULL);

    ASSERT_EQ_INT(0, run_cap(&ctx, L"del f1.txt f2.txt", out, sizeof(out)));
    ASSERT_EQ_INT(0, run_cap(&ctx, L"rmdir /s /q sub1", out, sizeof(out)));

    SetCurrentDirectoryW(orig_cwd);
    RemoveDirectoryW(root);
    shell_ctx_free(&ctx);
}

TEST_CASE(test_builtins_sys_info) {
    shell_ctx_t ctx;
    ASSERT_TRUE(shell_ctx_init(&ctx, NULL, NULL));
    char out[2048];

    ASSERT_EQ_INT(0, run_cap(&ctx, L"ver", out, sizeof(out)));
    ASSERT_TRUE(strstr(out, "Windows") != NULL);

    ASSERT_EQ_INT(0, run_cap(&ctx, L"vol", out, sizeof(out)));
    ASSERT_TRUE(strstr(out, "Serial Number") != NULL);

    ASSERT_EQ_INT(0, run_cap(&ctx, L"date /t", out, sizeof(out)));
    ASSERT_TRUE(strlen(out) > 2);

    ASSERT_EQ_INT(0, run_cap(&ctx, L"time /t", out, sizeof(out)));
    ASSERT_TRUE(strchr(out, ':') != NULL);

    ASSERT_EQ_INT(0, run_cap(&ctx, L"echo.", out, sizeof(out)));
    ASSERT_EQ_WSTR(L"\r\n", L"\r\n");

    shell_ctx_free(&ctx);
}

int wmain(void) {
    RUN_TEST(test_builtins_set_arith_and_setlocal);
    RUN_TEST(test_builtins_fs_ops);
    RUN_TEST(test_builtins_sys_info);
    TEST_SUMMARY();
}
