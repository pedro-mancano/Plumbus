#include "test_framework.h"
#include "exec.h"
#include "backend.h"
#include <windows.h>
#include <stdio.h>
#include <string.h>

TEST_CASE(test_sync_tail_safety_guard) {
    /* Unsafe lines that must NOT receive appended & call --__sync-state */
    ASSERT_TRUE(!exec_is_safe_for_sync_tail(L"echo \"a'"));
    ASSERT_TRUE(!exec_is_safe_for_sync_tail(L"echo \"a\" \"b"));
    ASSERT_TRUE(!exec_is_safe_for_sync_tail(L"echo hi^"));
    ASSERT_TRUE(!exec_is_safe_for_sync_tail(L"echo hi^^^"));
    ASSERT_TRUE(!exec_is_safe_for_sync_tail(L"echo hi &"));
    ASSERT_TRUE(!exec_is_safe_for_sync_tail(L"echo hi &&"));
    ASSERT_TRUE(!exec_is_safe_for_sync_tail(L"echo hi |"));
    ASSERT_TRUE(!exec_is_safe_for_sync_tail(L"(echo broken"));
    ASSERT_TRUE(!exec_is_safe_for_sync_tail(L"echo broken)"));
    ASSERT_TRUE(!exec_is_safe_for_sync_tail(L"   "));

    /* Safe lines */
    ASSERT_TRUE(exec_is_safe_for_sync_tail(L"echo \"a\""));
    ASSERT_TRUE(exec_is_safe_for_sync_tail(L"echo \"a & b\""));
    ASSERT_TRUE(exec_is_safe_for_sync_tail(L"echo hi^^"));
    ASSERT_TRUE(exec_is_safe_for_sync_tail(L"(echo ok)"));
    ASSERT_TRUE(exec_is_safe_for_sync_tail(L"echo \"a(b\""));
    ASSERT_TRUE(exec_is_safe_for_sync_tail(L"echo a ^& b"));
}

static int run_interactive_captured(shell_ctx_t *ctx, const wchar_t *line, char *out_buf, size_t out_cap) {
    HANDLE orig_out = GetStdHandle(STD_OUTPUT_HANDLE);
    HANDLE orig_err = GetStdHandle(STD_ERROR_HANDLE);

    SECURITY_ATTRIBUTES sa = { sizeof(sa), NULL, TRUE };
    HANDLE r = NULL, w = NULL;
    CreatePipe(&r, &w, &sa, 0);
    SetHandleInformation(r, HANDLE_FLAG_INHERIT, 0);

    SetStdHandle(STD_OUTPUT_HANDLE, w);
    SetStdHandle(STD_ERROR_HANDLE, w);

    int ec = exec_interactive_line(ctx, line);

    SetStdHandle(STD_OUTPUT_HANDLE, orig_out);
    SetStdHandle(STD_ERROR_HANDLE, orig_err);
    CloseHandle(w);

    size_t total = 0;
    DWORD rd = 0;
    while (total + 1 < out_cap && ReadFile(r, out_buf + total, (DWORD)(out_cap - 1 - total), &rd, NULL) && rd > 0) {
        total += rd;
    }
    out_buf[total] = '\0';
    CloseHandle(r);
    return ec;
}

TEST_CASE(test_exec_edge_cases_no_sync_leak) {
    wchar_t backend[MAX_PATH * 2];
    ASSERT_EQ_INT(BACKEND_OK, backend_resolve_cmd(backend, MAX_PATH * 2));

    shell_ctx_t ctx;
    ASSERT_TRUE(shell_ctx_init(&ctx, NULL, backend));
    wcsncpy(ctx.self_path, PLUMBUS_EXE_PATH, (sizeof(ctx.self_path) / sizeof(ctx.self_path[0])) - 1);

    char out[4096];

    /* 1. The exact reported bug: echo "a' */
    ASSERT_EQ_INT(0, run_interactive_captured(&ctx, L"echo \"a'", out, sizeof(out)));
    ASSERT_TRUE(strstr(out, "\"a'") != NULL);
    ASSERT_TRUE(strstr(out, "--__sync-state") == NULL);
    ASSERT_TRUE(strstr(out, "plumbus") == NULL);

    /* 2. Unclosed double quote */
    ASSERT_EQ_INT(0, run_interactive_captured(&ctx, L"echo \"hello", out, sizeof(out)));
    ASSERT_TRUE(strstr(out, "\"hello") != NULL);
    ASSERT_TRUE(strstr(out, "--__sync-state") == NULL);

    /* 3. Odd quote count with prior pair */
    ASSERT_EQ_INT(0, run_interactive_captured(&ctx, L"echo \"a\" \"b", out, sizeof(out)));
    ASSERT_TRUE(strstr(out, "\"a\" \"b") != NULL);
    ASSERT_TRUE(strstr(out, "--__sync-state") == NULL);

    /* 4. Single quotes */
    ASSERT_EQ_INT(0, run_interactive_captured(&ctx, L"echo 'single'", out, sizeof(out)));
    ASSERT_TRUE(strstr(out, "'single'") != NULL);
    ASSERT_TRUE(strstr(out, "--__sync-state") == NULL);

    /* 5. Trailing caret (^) */
    run_interactive_captured(&ctx, L"echo hi^", out, sizeof(out));
    ASSERT_TRUE(strstr(out, "--__sync-state") == NULL);

    /* 6. Escaped trailing caret (^^) */
    ASSERT_EQ_INT(0, run_interactive_captured(&ctx, L"echo hi^^", out, sizeof(out)));
    ASSERT_TRUE(strstr(out, "hi^") != NULL);
    ASSERT_TRUE(strstr(out, "--__sync-state") == NULL);

    /* 7. Quoted & */
    ASSERT_EQ_INT(0, run_interactive_captured(&ctx, L"echo \"a & b\"", out, sizeof(out)));
    ASSERT_TRUE(strstr(out, "\"a & b\"") != NULL);
    ASSERT_TRUE(strstr(out, "--__sync-state") == NULL);

    /* 8. Caret-escaped & */
    ASSERT_EQ_INT(0, run_interactive_captured(&ctx, L"echo a ^& b", out, sizeof(out)));
    ASSERT_TRUE(strstr(out, "a & b") != NULL);
    ASSERT_TRUE(strstr(out, "--__sync-state") == NULL);

    /* 9 & 10. Comments rem / :: must not execute or leak, and preserve last_exit_code */
    ctx.last_exit_code = 77;
    ASSERT_EQ_INT(77, run_interactive_captured(&ctx, L"rem comment & echo LEAK", out, sizeof(out)));
    ASSERT_EQ_INT(0, (int)strlen(out));
    ASSERT_EQ_INT(77, run_interactive_captured(&ctx, L":: comment & echo LEAK", out, sizeof(out)));
    ASSERT_EQ_INT(0, (int)strlen(out));

    /* 11. echo. */
    ASSERT_EQ_INT(0, run_interactive_captured(&ctx, L"echo.", out, sizeof(out)));
    ASSERT_TRUE(strstr(out, "--__sync-state") == NULL);

    /* 12. Native simple set with unclosed quote */
    ASSERT_EQ_INT(0, run_interactive_captured(&ctx, L"set PLUMBUS_UNCLOSED=\"abc", out, sizeof(out)));
    wchar_t val[128];
    ASSERT_TRUE(GetEnvironmentVariableW(L"PLUMBUS_UNCLOSED", val, 128) > 0);
    ASSERT_EQ_WSTR(L"\"abc", val);
    SetEnvironmentVariableW(L"PLUMBUS_UNCLOSED", NULL);

    /* 13. Quoted set with & */
    ASSERT_EQ_INT(0, run_interactive_captured(&ctx, L"set \"PLUMBUS_AMP=foo&bar\"", out, sizeof(out)));
    ASSERT_TRUE(GetEnvironmentVariableW(L"PLUMBUS_AMP", val, 128) > 0);
    ASSERT_EQ_WSTR(L"foo&bar", val);
    SetEnvironmentVariableW(L"PLUMBUS_AMP", NULL);

    /* 14. Unbalanced parenthesis */
    int ec_paren = run_interactive_captured(&ctx, L"(echo broken", out, sizeof(out));
    ASSERT_TRUE(ec_paren != 0);
    ASSERT_TRUE(strstr(out, "--__sync-state") == NULL);

    /* 15. exit /b 13 */
    ASSERT_EQ_INT(13, run_interactive_captured(&ctx, L"echo bye & exit /b 13", out, sizeof(out)));
    ASSERT_TRUE(strstr(out, "bye") != NULL);
    ASSERT_TRUE(strstr(out, "--__sync-state") == NULL);

    /* 16. for %i loop */
    ASSERT_EQ_INT(0, run_interactive_captured(&ctx, L"for %i in (1 2) do @echo ITEM_%i", out, sizeof(out)));
    ASSERT_TRUE(strstr(out, "ITEM_1") != NULL);
    ASSERT_TRUE(strstr(out, "ITEM_2") != NULL);
    ASSERT_TRUE(strstr(out, "--__sync-state") == NULL);

    shell_ctx_free(&ctx);
}

int wmain(void) {
    RUN_TEST(test_sync_tail_safety_guard);
    RUN_TEST(test_exec_edge_cases_no_sync_leak);
    TEST_SUMMARY();
}
