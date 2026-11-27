#include "test_framework.h"
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void ensure_dir_w(const wchar_t *p) {
    CreateDirectoryW(p, NULL);
}

static void rmdir_tree_w(const wchar_t *path) {
    wchar_t pat[MAX_PATH * 2];
    swprintf(pat, MAX_PATH * 2, L"%ls\\*", path);
    WIN32_FIND_DATAW fd;
    HANDLE hf = FindFirstFileW(pat, &fd);
    if (hf != INVALID_HANDLE_VALUE) {
        do {
            if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) continue;
            wchar_t child[MAX_PATH * 2];
            swprintf(child, MAX_PATH * 2, L"%ls\\%ls", path, fd.cFileName);
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_READONLY) {
                SetFileAttributesW(child, fd.dwFileAttributes & ~FILE_ATTRIBUTE_READONLY);
            }
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                rmdir_tree_w(child);
            } else {
                DeleteFileW(child);
            }
        } while (FindNextFileW(hf, &fd));
        FindClose(hf);
    }
    RemoveDirectoryW(path);
}

static void write_bat_w(const wchar_t *path, const char *content) {
    HANDLE h = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h != INVALID_HANDLE_VALUE) {
        DWORD w = 0;
        WriteFile(h, content, (DWORD)strlen(content), &w, NULL);
        CloseHandle(h);
    }
}

static const wchar_t *get_ref_cmd_path(void) {
    static wchar_t path[MAX_PATH * 2];
    if (!path[0]) {
        wcsncpy(path, PLUMBUS_BACKUP_CMD_PATH, (sizeof(path) / sizeof(path[0])) - 1);
        path[(sizeof(path) / sizeof(path[0])) - 1] = L'\0';
        for (wchar_t *s = path; *s; s++) {
            if (*s == L'/') *s = L'\\';
        }
    }
    DWORD a = GetFileAttributesW(path);
    if (a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY)) {
        return path;
    }
    return L"C:\\Windows\\System32\\cmd.exe";
}

static int run_exe_raw(const wchar_t *exe_path, const wchar_t *args, const wchar_t *cwd,
                       char *out_buf, size_t out_cap) {
    SECURITY_ATTRIBUTES sa = { sizeof(sa), NULL, TRUE };
    HANDLE r = NULL, w = NULL, in_r = NULL, in_w = NULL;
    if (!CreatePipe(&r, &w, &sa, 0)) return -1;
    if (!CreatePipe(&in_r, &in_w, &sa, 0)) { CloseHandle(r); CloseHandle(w); return -1; }
    SetHandleInformation(r, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(in_w, HANDLE_FLAG_INHERIT, 0);
    CloseHandle(in_w);

    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof(si));
    ZeroMemory(&pi, sizeof(pi));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = in_r;
    si.hStdOutput = w;
    si.hStdError = w;

    wchar_t cmdline[8192];
    swprintf(cmdline, 8192, L"\"%ls\" %ls", exe_path, args);

    BOOL ok = CreateProcessW(exe_path, cmdline, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, cwd, &si, &pi);
    CloseHandle(w);
    CloseHandle(in_r);
    if (!ok) {
        CloseHandle(r);
        return -1;
    }

    size_t tot = 0;
    ULONGLONG start = GetTickCount64();
    bool timed_out = false;
    for (;;) {
        DWORD avail = 0;
        while (PeekNamedPipe(r, NULL, 0, NULL, &avail, NULL) && avail > 0) {
            if (tot + 1 >= out_cap) break;
            DWORD to_read = avail;
            if (to_read > (DWORD)(out_cap - 1 - tot)) to_read = (DWORD)(out_cap - 1 - tot);
            DWORD rd = 0;
            if (!ReadFile(r, out_buf + tot, to_read, &rd, NULL) || rd == 0) break;
            tot += rd;
        }
        DWORD wrc = WaitForSingleObject(pi.hProcess, 10);
        if (wrc == WAIT_OBJECT_0) {
            while (PeekNamedPipe(r, NULL, 0, NULL, &avail, NULL) && avail > 0 && tot + 1 < out_cap) {
                DWORD to_read = avail;
                if (to_read > (DWORD)(out_cap - 1 - tot)) to_read = (DWORD)(out_cap - 1 - tot);
                DWORD rd = 0;
                if (!ReadFile(r, out_buf + tot, to_read, &rd, NULL) || rd == 0) break;
                tot += rd;
            }
            break;
        }
        if (GetTickCount64() - start >= 3000) {
            TerminateProcess(pi.hProcess, 999);
            timed_out = true;
            break;
        }
    }
    out_buf[tot] = '\0';
    CloseHandle(r);

    if (timed_out) {
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        return -2;
    }
    DWORD ec = 0;
    GetExitCodeProcess(pi.hProcess, &ec);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return (int)ec;
}

static void normalize_dir_utf8(char *buf, const wchar_t *dir_w) {
    char dir_u8[MAX_PATH * 2];
    int n = WideCharToMultiByte(CP_UTF8, 0, dir_w, -1, dir_u8, sizeof(dir_u8), NULL, NULL);
    if (n <= 1) return;
    size_t dlen = strlen(dir_u8);
    char *p = buf;
    while ((p = strstr(p, dir_u8)) != NULL) {
        const char *tag = "<DIR>";
        size_t tlen = 5;
        memmove(p + tlen, p + dlen, strlen(p + dlen) + 1);
        memcpy(p, tag, tlen);
        p += tlen;
    }
}

static bool assert_parity_case(const char *name, const wchar_t *args, const char *bat_content) {
    wchar_t root[MAX_PATH];
    swprintf(root, MAX_PATH, L"%ls\\parity", PLUMBUS_TEST_SANDBOX_DIR);
    ensure_dir_w(PLUMBUS_TEST_SANDBOX_DIR);
    ensure_dir_w(root);

    wchar_t d_cmd[MAX_PATH], d_plu[MAX_PATH];
    swprintf(d_cmd, MAX_PATH, L"%ls\\cmd_box", root);
    swprintf(d_plu, MAX_PATH, L"%ls\\plu_box", root);
    for (wchar_t *s = d_cmd; *s; s++) if (*s == L'/') *s = L'\\';
    for (wchar_t *s = d_plu; *s; s++) if (*s == L'/') *s = L'\\';

    rmdir_tree_w(d_cmd);
    rmdir_tree_w(d_plu);
    ensure_dir_w(d_cmd);
    ensure_dir_w(d_plu);

    if (bat_content) {
        wchar_t b1[MAX_PATH], b2[MAX_PATH];
        swprintf(b1, MAX_PATH, L"%ls\\test.bat", d_cmd);
        swprintf(b2, MAX_PATH, L"%ls\\test.bat", d_plu);
        write_bat_w(b1, bat_content);
        write_bat_w(b2, bat_content);
    }

    char out1[16384], out2[16384];
    int rc1 = run_exe_raw(get_ref_cmd_path(), args, d_cmd, out1, sizeof(out1));
    int rc2 = run_exe_raw(PLUMBUS_EXE_PATH, args, d_plu, out2, sizeof(out2));
    normalize_dir_utf8(out1, d_cmd);
    normalize_dir_utf8(out2, d_plu);

    rmdir_tree_w(d_cmd);
    rmdir_tree_w(d_plu);

    if (rc1 != rc2 || strcmp(out1, out2) != 0) {
        fprintf(stderr, "  PARITY DIFF [%s]: rc cmd=%d plu=%d\n  CMD: [%s]\n  PLU: [%s]\n",
                name, rc1, rc2, out1, out2);
        fflush(stderr);
        return false;
    }
    return true;
}

#define PARITY_CHECK(name, args, bat) ASSERT_TRUE(assert_parity_case(name, args, bat))

TEST_CASE(test_parity_echo_and_quotes) {
    PARITY_CHECK("echo_basic", L"/d /c echo hello world", NULL);
    PARITY_CHECK("echo_spaces", L"/d /c echo   leading   spaces", NULL);
    PARITY_CHECK("echo_dot", L"/d /c echo.", NULL);
    PARITY_CHECK("echo_dot_text", L"/d /c echo.hello", NULL);
    PARITY_CHECK("echo_colon", L"/d /c echo:hello", NULL);
    PARITY_CHECK("echo_unclosed_q1", L"/d /c echo \"a'", NULL);
    PARITY_CHECK("echo_unclosed_q2", L"/d /c echo a\"", NULL);
    PARITY_CHECK("echo_caret_amp", L"/d /c echo a ^& b", NULL);
    PARITY_CHECK("echo_double_caret", L"/d /c echo hi^^", NULL);
    PARITY_CHECK("echo_quoted_amp", L"/d /c echo \"a & b\"", NULL);
    PARITY_CHECK("echo_tab", L"/d /c echo\thello", NULL);
}

TEST_CASE(test_parity_ops_redirs_pipes) {
    PARITY_CHECK("op_seq", L"/d /c echo 1 & echo 2", NULL);
    PARITY_CHECK("op_and_ok", L"/d /c echo 1 && echo 2", NULL);
    PARITY_CHECK("op_and_fail", L"/d /c exit /b 5 && echo NO", NULL);
    PARITY_CHECK("op_or_fail", L"/d /c (exit /b 5) || echo RECOVERED", NULL);
    PARITY_CHECK("op_or_ok", L"/d /c echo OK || echo NO", NULL);
    PARITY_CHECK("op_block_redir", L"/d /c (echo L1 & echo L2) > f.txt & type f.txt", NULL);
    PARITY_CHECK("op_pipe_2stage", L"/d /c echo hello_pipe | findstr hello_pipe", NULL);
    PARITY_CHECK("op_pipe_3stage_iso",
        L"/d /c \"set P=orig & (set P=chg & echo banana & echo apple ) | findstr a | sort & call echo P=%P%\"",
        NULL);
    PARITY_CHECK("op_redir_append", L"/d /c echo A>a.txt & echo B>>a.txt & type a.txt", NULL);
    PARITY_CHECK("op_redir_before", L"/d /c >pre.txt echo BEFORE & type pre.txt", NULL);
    PARITY_CHECK("op_redir_2to1", L"/d /c (echo OUT & echo ERR 1>&2) > c.txt 2>&1 & type c.txt", NULL);
}

TEST_CASE(test_parity_set_vars_and_if_for) {
    PARITY_CHECK("set_unquoted_ws", L"/d /c set V=123 & call echo [%V%]", NULL);
    PARITY_CHECK("set_quoted_amp", L"/d /c set \"V=a&b\" & if defined V echo DEF_OK", NULL);
    PARITY_CHECK("set_arith_interactive", L"/d /c set /a 2+3", NULL);
    PARITY_CHECK("set_arith_compound", L"/d /c set /a X=10 & set /a X+=5 & set /a X*=2 & call echo %X%", NULL);
    PARITY_CHECK("var_substr", L"/d /c set S=abcdefg & call echo %S:~2,3% %S:~-3% %S:~1,-2%", NULL);
    PARITY_CHECK("var_subst", L"/d /c set S=foo_bar_foo & call echo %S:foo=baz% %S:*_=X%", NULL);
    PARITY_CHECK("var_cmdextver", L"/d /c echo %CMDEXTVERSION%", NULL);

    PARITY_CHECK("if_eq_ci", L"/d /c if /i abc==ABC echo YES", NULL);
    PARITY_CHECK("if_not", L"/d /c if not 1==2 echo DIFF", NULL);
    PARITY_CHECK("if_num", L"/d /c if 10 gtr 2 (echo NUM) else (echo STR)", NULL);
    PARITY_CHECK("if_neg", L"/d /c if -5 lss 0 (echo NEG) else (echo POS)", NULL);
    PARITY_CHECK("if_defined", L"/d /c set MYD=1 & if defined MYD (echo DEF) else (echo UNDEF)", NULL);
    PARITY_CHECK("if_else_if", L"/d /c if 1==2 (echo A) else if 2==2 (echo B) else (echo C)", NULL);

    PARITY_CHECK("for_plain", L"/d /c for %i in (a b c) do @echo [%i]", NULL);
    PARITY_CHECK("for_l_pos", L"/d /c for /L %i in (1,2,7) do @echo %i", NULL);
    PARITY_CHECK("for_l_neg", L"/d /c for /L %i in (5,-2,1) do @echo %i", NULL);
    PARITY_CHECK("for_f_cmd", L"/d /c for /f %i in ('echo HELLO_CMD') do @echo %i", NULL);
}

TEST_CASE(test_parity_full_batch_scripts) {
    PARITY_CHECK("bat_delayed_for", L"/d /c test.bat",
        "@echo off\r\n"
        "setlocal enabledelayedexpansion\r\n"
        "set SUM=0\r\n"
        "for /L %%i in (1,1,5) do (\r\n"
        "  set /a SUM+=%%i\r\n"
        "  echo step %%i sum=!SUM!\r\n"
        ")\r\n"
        "echo FINAL=!SUM!\r\n"
        "endlocal\r\n"
        "echo AFTER=%SUM%\r\n");

    PARITY_CHECK("bat_args_shift_call", L"/d /c test.bat \"one 1\" two three",
        "@echo off\r\n"
        "echo A0=%~nx0 A1=%1 U1=%~1 A2=%2 ALL=%*\r\n"
        "shift\r\n"
        "echo S0 A0=%~nx0 A1=%1 A2=%2\r\n"
        "shift /1\r\n"
        "echo S1 A0=%~nx0 A1=%1 A2=%2\r\n"
        "call :sub foo \"bar baz\"\r\n"
        "echo SUB_EC=%ERRORLEVEL%\r\n"
        "goto :eof\r\n"
        "echo DEAD\r\n"
        ":sub\r\n"
        "echo IN_SUB 0=%0 1=%1 2=%2 ~2=%~2\r\n"
        "exit /b 9\r\n");

    PARITY_CHECK("bat_setlocal_tunnel", L"/d /c test.bat",
        "@echo off\r\n"
        "set X=outer\r\n"
        "setlocal\r\n"
        "set X=inner\r\n"
        "mkdir subA\\subB\r\n"
        "cd subA\\subB\r\n"
        "echo INSIDE=%X% %CD%\r\n"
        "endlocal & set OUT=%X%\r\n"
        "echo OUTSIDE=%X% OUT=%OUT% %CD%\r\n");

    PARITY_CHECK("bat_for_f_ranges_delims", L"/d /c test.bat",
        "@echo off\r\n"
        "for /f \"tokens=1-3 delims=,\" %%a in (\"a,b,c,d\") do echo %%a-%%b-%%c\r\n"
        "for /f \"tokens=1,3* delims=,\" %%a in (\"p1,p2,p3,p4,p5\") do echo %%a^|%%b^|%%c\r\n"
        "for /f \"delims=\" %%a in (\"whole line with spaces\") do echo [%%a]\r\n"
        "echo ;skipme>f.txt\r\n"
        "echo first>>f.txt\r\n"
        "echo second>>f.txt\r\n"
        "for /f \"skip=1 eol=;\" %%i in (f.txt) do echo LINE=%%i\r\n");

    PARITY_CHECK("bat_fs_pipeline", L"/d /c test.bat",
        "@echo off\r\n"
        "echo line1>d1.txt\r\n"
        "echo line2>d2.txt\r\n"
        "copy /y d1.txt+d2.txt d3.txt >nul\r\n"
        "type d3.txt\r\n"
        "ren *.txt *.bak\r\n"
        "dir /b /o:n *.bak\r\n"
        "mkdir target_dir\r\n"
        "move /y d3.bak target_dir >nul\r\n"
        "dir /b /s target_dir\r\n"
        "del /q *.bak\r\n"
        "rmdir /s /q target_dir\r\n"
        "if not exist target_dir\\ echo CLEAN_OK\r\n");

    PARITY_CHECK("bat_set_a_and_if_edge", L"/d /c test.bat",
        "@echo off\r\n"
        "set /a X=2+3*4, Y=(10-2)/2, Z=15%%4\r\n"
        "echo %X% %Y% %Z%\r\n"
        "set /a H=0x10, O=010, N=-5, INV=~0, NOT0=!0, NOT5=!5\r\n"
        "echo %H% %O% %N% %INV% %NOT0% %NOT5%\r\n"
        "set /a \"A=(1<<4)|3, B=A^5, C=B&7\"\r\n"
        "echo %A% %B% %C%\r\n"
        "set /a U=UNDEF_XYZ+7\r\n"
        "echo %U%\r\n"
        "mkdir mydir\r\n"
        "if exist mydir\\ echo DIR1\r\n"
        "if 010 equ 8 echo OCT1\r\n"
        "if 0x10 equ 16 echo HEX1\r\n"
        "if \"a\" lss \"b\" echo LSS1\r\n"
        "rmdir mydir\r\n");

    PARITY_CHECK("bat_call_transfer_and_caret", L"/d /c test.bat",
        "@echo off\r\n"
        "echo part1 ^\r\n"
        "part2\r\n"
        "echo 100%% done\r\n"
        "echo @echo IN_OTHER %%1>other.bat\r\n"
        "(echo @exit /b 4)>>other.bat\r\n"
        "call other.bat hello\r\n"
        "echo AFTER_CALL=%ERRORLEVEL%\r\n"
        "other.bat direct\r\n"
        "echo NEVER_REACHED\r\n");

    PARITY_CHECK("bat_multiline_if_and_rec", L"/d /c test.bat",
        "@echo off\r\n"
        "set F=1\r\n"
        "if %F%==1 (\r\n"
        "  echo T1\r\n"
        "  echo T2\r\n"
        ") else (\r\n"
        "  echo F1\r\n"
        ")\r\n"
        "call :rec 3\r\n"
        "goto :eof\r\n"
        ":rec\r\n"
        "setlocal\r\n"
        "set N=%1\r\n"
        "if %N% leq 0 goto :eof\r\n"
        "echo REC=%N%\r\n"
        "set /a NEXT=N-1\r\n"
        "call :rec %NEXT%\r\n"
        "endlocal\r\n"
        "exit /b 0\r\n");
}

int wmain(void) {
    RUN_TEST(test_parity_echo_and_quotes);
    RUN_TEST(test_parity_ops_redirs_pipes);
    RUN_TEST(test_parity_set_vars_and_if_for);
    RUN_TEST(test_parity_full_batch_scripts);
    TEST_SUMMARY();
}
