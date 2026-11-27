#include "test_framework.h"
#include "cmd_expand.h"
#include <windows.h>
#include <stdlib.h>

TEST_CASE(test_expand_dynamic_pseudo_vars) {
    shell_ctx_t ctx;
    ASSERT_TRUE(shell_ctx_init(&ctx, NULL, NULL));
    ctx.last_exit_code = 42;

    wchar_t *val = NULL;
    ASSERT_TRUE(cmd_lookup_var(&ctx, L"CD", &val));
    ASSERT_TRUE(wcslen(val) >= 3);
    free(val);

    ASSERT_TRUE(cmd_lookup_var(&ctx, L"ERRORLEVEL", &val));
    ASSERT_EQ_WSTR(L"42", val);
    free(val);

    ASSERT_TRUE(cmd_lookup_var(&ctx, L"CMDEXTVERSION", &val));
    ASSERT_EQ_WSTR(L"2", val);
    free(val);

    ASSERT_TRUE(cmd_lookup_var(&ctx, L"CMDCMDLINE", &val));
    ASSERT_TRUE(wcslen(val) > 0);
    free(val);

    ASSERT_TRUE(cmd_lookup_var(&ctx, L"DATE", &val));
    ASSERT_TRUE(wcslen(val) > 0);
    free(val);

    ASSERT_TRUE(cmd_lookup_var(&ctx, L"TIME", &val));
    ASSERT_TRUE(wcslen(val) > 0);
    free(val);

    ASSERT_TRUE(cmd_lookup_var(&ctx, L"RANDOM", &val));
    int rnd = _wtoi(val);
    ASSERT_TRUE(rnd >= 0 && rnd < 32768);
    free(val);

    ASSERT_TRUE(cmd_lookup_var(&ctx, L"HIGHESTNUMANODENUMBER", &val));
    ASSERT_TRUE(wcslen(val) > 0);
    free(val);

    shell_ctx_free(&ctx);
}

TEST_CASE(test_expand_percent_and_modifiers) {
    shell_ctx_t ctx;
    ASSERT_TRUE(shell_ctx_init(&ctx, NULL, NULL));
    SetEnvironmentVariableW(L"PLUMBUS_EXP_VAR", L"hello_world_test");

    wchar_t *s1 = cmd_expand_percent(&ctx, L"[%PLUMBUS_EXP_VAR%]", false);
    ASSERT_EQ_WSTR(L"[hello_world_test]", s1);
    free(s1);

    wchar_t *s2 = cmd_expand_percent(&ctx, L"%PLUMBUS_EXP_VAR:world=plumbus%", false);
    ASSERT_EQ_WSTR(L"hello_plumbus_test", s2);
    free(s2);

    wchar_t *s3 = cmd_expand_percent(&ctx, L"%PLUMBUS_EXP_VAR:~6,5%", false);
    ASSERT_EQ_WSTR(L"world", s3);
    free(s3);

    wchar_t *s4 = cmd_expand_percent(&ctx, L"%PLUMBUS_EXP_VAR:~-4%", false);
    ASSERT_EQ_WSTR(L"test", s4);
    free(s4);

    /* Interactive keeps unknown literal; batch drops it */
    wchar_t *s5 = cmd_expand_percent(&ctx, L"%PLUMBUS_NO_SUCH_VAR%", false);
    ASSERT_EQ_WSTR(L"%PLUMBUS_NO_SUCH_VAR%", s5);
    free(s5);

    wchar_t *s6 = cmd_expand_percent(&ctx, L"X%PLUMBUS_NO_SUCH_VAR%Y", true);
    ASSERT_EQ_WSTR(L"XY", s6);
    free(s6);

    SetEnvironmentVariableW(L"PLUMBUS_EXP_VAR", NULL);
    shell_ctx_free(&ctx);
}

TEST_CASE(test_expand_delayed_and_tilde) {
    shell_ctx_t ctx;
    ASSERT_TRUE(shell_ctx_init(&ctx, NULL, NULL));
    ctx.delayed_expansion = true;
    SetEnvironmentVariableW(L"PLUMBUS_DEL", L"abc_xyz");

    wchar_t *d1 = cmd_expand_delayed(&ctx, L"!PLUMBUS_DEL:xyz=123! ^!lit^!");
    ASSERT_EQ_WSTR(L"abc_123 !lit!", d1);
    free(d1);

    wchar_t *t1 = cmd_apply_tilde_mods(L"\"C:\\Windows\\System32\\notepad.exe\"", L"nx");
    ASSERT_EQ_WSTR(L"notepad.exe", t1);
    free(t1);

    wchar_t *t2 = cmd_apply_tilde_mods(L"C:\\Windows\\System32\\notepad.exe", L"dp");
    ASSERT_EQ_WSTR(L"C:\\Windows\\System32\\", t2);
    free(t2);

    SetEnvironmentVariableW(L"PLUMBUS_DEL", NULL);
    shell_ctx_free(&ctx);
}

int wmain(void) {
    RUN_TEST(test_expand_dynamic_pseudo_vars);
    RUN_TEST(test_expand_percent_and_modifiers);
    RUN_TEST(test_expand_delayed_and_tilde);
    TEST_SUMMARY();
}
