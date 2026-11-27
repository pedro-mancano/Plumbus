#include "test_framework.h"
#include "cmdline.h"
#include <windows.h>

TEST_CASE(test_cmdline_basic_c_flag) {
    cmd_options_t o;
    ASSERT_TRUE(cmdline_parse(L"plumbus.exe /c \"echo hello\"", &o));
    ASSERT_EQ_INT(CMD_MODE_EXEC_EXIT, o.mode);
    ASSERT_EQ_WSTR(L"echo hello", o.command_parsed);
    cmdline_free(&o);
}

TEST_CASE(test_cmdline_glued_and_case_flags) {
    cmd_options_t o;
    ASSERT_TRUE(cmdline_parse(L"\"C:\\Program Files\\plumbus.exe\" /D/Q/V:ON/E:OFF/T:0A/C\"echo glued\"", &o));
    ASSERT_EQ_INT(CMD_MODE_EXEC_EXIT, o.mode);
    ASSERT_TRUE(o.disable_autorun);
    ASSERT_TRUE(o.echo_off);
    ASSERT_TRUE(o.delayed_expansion_set && o.delayed_expansion);
    ASSERT_TRUE(o.extensions_set && !o.extensions);
    ASSERT_EQ_WSTR(L"0A", o.color_attr);
    ASSERT_EQ_WSTR(L"echo glued", o.command_parsed);
    cmdline_free(&o);
}

TEST_CASE(test_cmdline_quote_gt2_and_special) {
    cmd_options_t o;
    ASSERT_TRUE(cmdline_parse(L"plumbus.exe /d /c \"\"prog dir\\hello.bat\" \"arg 1\"\"", &o));
    ASSERT_EQ_WSTR(L"\"prog dir\\hello.bat\" \"arg 1\"", o.command_parsed);
    cmdline_free(&o);

    ASSERT_TRUE(cmdline_parse(L"plumbus.exe /c \"echo a&echo b\"", &o));
    ASSERT_EQ_WSTR(L"echo a&echo b", o.command_parsed);
    cmdline_free(&o);
}

TEST_CASE(test_cmdline_slash_s_forces_strip) {
    wchar_t *stripped = cmdline_strip_cmd_quotes(L" \"C:\\Windows\\System32\\cmd.exe\"", true);
    ASSERT_EQ_WSTR(L"C:\\Windows\\System32\\cmd.exe", stripped);
    free(stripped);
}

TEST_CASE(test_cmdline_two_quotes_existing_exe_preserved) {
    wchar_t tmp[MAX_PATH];
    GetTempPathW(MAX_PATH, tmp);
    wchar_t dir[MAX_PATH];
    swprintf(dir, MAX_PATH, L"%lsplumbus_qtest dir", tmp);
    CreateDirectoryW(dir, NULL);
    wchar_t bat[MAX_PATH];
    swprintf(bat, MAX_PATH, L"%ls\\hello.bat", dir);
    HANDLE h = CreateFileW(bat, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h != INVALID_HANDLE_VALUE) CloseHandle(h);

    wchar_t arg[MAX_PATH + 8];
    swprintf(arg, MAX_PATH + 8, L"\"%ls\"", bat);
    wchar_t *res = cmdline_strip_cmd_quotes(arg, false);
    ASSERT_EQ_WSTR(arg, res);
    free(res);

    wchar_t *res_s = cmdline_strip_cmd_quotes(arg, true);
    ASSERT_EQ_WSTR(bat, res_s);
    free(res_s);

    DeleteFileW(bat);
    RemoveDirectoryW(dir);
}

TEST_CASE(test_cmdline_k_u_a_help_and_sync) {
    cmd_options_t o;
    ASSERT_TRUE(cmdline_parse(L"plumbus.exe /u /k echo hi", &o));
    ASSERT_EQ_INT(CMD_MODE_EXEC_KEEP, o.mode);
    ASSERT_TRUE(o.output_unicode);
    ASSERT_EQ_WSTR(L"echo hi", o.command_parsed);
    cmdline_free(&o);

    ASSERT_TRUE(cmdline_parse(L"plumbus.exe /?", &o));
    ASSERT_EQ_INT(CMD_MODE_HELP, o.mode);
    cmdline_free(&o);

    ASSERT_TRUE(cmdline_parse(L"plumbus.exe --__sync-state \"C:\\tmp\\a b.bin\" 42", &o));
    ASSERT_EQ_INT(CMD_MODE_INTERNAL_SYNC, o.mode);
    ASSERT_EQ_WSTR(L"C:\\tmp\\a b.bin", o.sync_file_path);
    ASSERT_EQ_INT(42, o.sync_exit_code);
    cmdline_free(&o);
}

TEST_CASE(test_cmdline_config_subcommand) {
    cmd_options_t o;
    ASSERT_TRUE(cmdline_parse(L"plumbus.exe config", &o));
    ASSERT_EQ_INT(CMD_MODE_CONFIG, o.mode);
    cmdline_free(&o);

    ASSERT_TRUE(cmdline_parse(L"\"C:\\Tools\\plumbus.exe\" /d config   ", &o));
    ASSERT_EQ_INT(CMD_MODE_CONFIG, o.mode);
    cmdline_free(&o);
}

int wmain(void) {
    RUN_TEST(test_cmdline_basic_c_flag);
    RUN_TEST(test_cmdline_glued_and_case_flags);
    RUN_TEST(test_cmdline_quote_gt2_and_special);
    RUN_TEST(test_cmdline_slash_s_forces_strip);
    RUN_TEST(test_cmdline_two_quotes_existing_exe_preserved);
    RUN_TEST(test_cmdline_k_u_a_help_and_sync);
    RUN_TEST(test_cmdline_config_subcommand);
    TEST_SUMMARY();
}
