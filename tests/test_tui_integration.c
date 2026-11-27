#include "test_framework.h"
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    HANDLE out_r;
    char *buf;
    size_t len;
    size_t cap;
    CRITICAL_SECTION cs;
} conpty_cap_t;

static DWORD WINAPI reader_thread(LPVOID arg) {
    conpty_cap_t *c = (conpty_cap_t *)arg;
    char tmp[4096];
    DWORD rd = 0;
    while (ReadFile(c->out_r, tmp, sizeof(tmp), &rd, NULL) && rd > 0) {
        EnterCriticalSection(&c->cs);
        if (c->len + rd + 1 > c->cap) {
            size_t ncap = c->cap ? c->cap * 2 : 16384;
            while (ncap < c->len + rd + 1) ncap *= 2;
            char *nb = (char *)realloc(c->buf, ncap);
            if (nb) { c->buf = nb; c->cap = ncap; }
        }
        if (c->len + rd + 1 <= c->cap) {
            memcpy(c->buf + c->len, tmp, rd);
            c->len += rd;
            c->buf[c->len] = '\0';
        }
        LeaveCriticalSection(&c->cs);
    }
    return 0;
}

static bool wait_for_substr(conpty_cap_t *c, size_t from_pos, const char *needle, DWORD timeout_ms) {
    DWORD start = GetTickCount();
    while (GetTickCount() - start < timeout_ms) {
        EnterCriticalSection(&c->cs);
        bool found = (c->len > from_pos && strstr(c->buf + from_pos, needle) != NULL);
        LeaveCriticalSection(&c->cs);
        if (found) return true;
        Sleep(25);
    }
    return false;
}

static size_t get_len(conpty_cap_t *c) {
    EnterCriticalSection(&c->cs);
    size_t l = c->len;
    LeaveCriticalSection(&c->cs);
    return l;
}

static void send_bytes(HANDLE in_w, const char *s) {
    DWORD wr = 0;
    WriteFile(in_w, s, (DWORD)strlen(s), &wr, NULL);
}

TEST_CASE(test_tui_f2_plumbus_config_and_ghost_clear) {
    wchar_t sandbox[MAX_PATH];
    wcsncpy(sandbox, PLUMBUS_TEST_SANDBOX_DIR, MAX_PATH - 1);
    sandbox[MAX_PATH - 1] = L'\0';
    for (wchar_t *p = sandbox; *p; p++) if (*p == L'/') *p = L'\\';
    CreateDirectoryW(sandbox, NULL);

    wchar_t rc_file[MAX_PATH], hist_file[MAX_PATH];
    swprintf(rc_file, MAX_PATH, L"%ls\\tui_test.cmdrc", sandbox);
    swprintf(hist_file, MAX_PATH, L"%ls\\tui_test.hist", sandbox);
    DeleteFileW(rc_file);

    /* Seed history for Case C (ghost text test) */
    {
        HANDLE hf = CreateFileW(hist_file, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        ASSERT_TRUE(hf != INVALID_HANDLE_VALUE);
        const char *seed = "echo hello_ghost_tail\n";
        DWORD w = 0;
        WriteFile(hf, seed, (DWORD)strlen(seed), &w, NULL);
        CloseHandle(hf);
    }

    SetEnvironmentVariableW(L"PLUMBUS_RC_FILE", rc_file);
    SetEnvironmentVariableW(L"PLUMBUS_HISTORY_FILE", hist_file);

    HANDLE in_r = NULL, in_w = NULL, out_r = NULL, out_w = NULL;
    ASSERT_TRUE(CreatePipe(&in_r, &in_w, NULL, 0));
    ASSERT_TRUE(CreatePipe(&out_r, &out_w, NULL, 0));

    HPCON hpc = NULL;
    COORD sz = { 100, 30 };
    HRESULT hr = CreatePseudoConsole(sz, in_r, out_w, 0, &hpc);
    CloseHandle(in_r);
    CloseHandle(out_w);
    ASSERT_TRUE(SUCCEEDED(hr));

    SIZE_T attr_sz = 0;
    InitializeProcThreadAttributeList(NULL, 1, 0, &attr_sz);
    LPPROC_THREAD_ATTRIBUTE_LIST attr_list = (LPPROC_THREAD_ATTRIBUTE_LIST)malloc(attr_sz);
    ASSERT_TRUE(attr_list != NULL);
    ASSERT_TRUE(InitializeProcThreadAttributeList(attr_list, 1, 0, &attr_sz));
    ASSERT_TRUE(UpdateProcThreadAttribute(attr_list, 0, PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE,
                                          hpc, sizeof(HPCON), NULL, NULL));

    STARTUPINFOEXW si;
    ZeroMemory(&si, sizeof(si));
    si.StartupInfo.cb = sizeof(si);
    si.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    si.lpAttributeList = attr_list;

    PROCESS_INFORMATION pi;
    ZeroMemory(&pi, sizeof(pi));
    wchar_t cmdline[MAX_PATH * 2];
    swprintf(cmdline, MAX_PATH * 2, L"\"%ls\" /d", PLUMBUS_EXE_PATH);

    BOOL ok = CreateProcessW(PLUMBUS_EXE_PATH, cmdline, NULL, NULL, FALSE,
                             EXTENDED_STARTUPINFO_PRESENT, NULL, sandbox,
                             &si.StartupInfo, &pi);
    ASSERT_TRUE(ok);

    conpty_cap_t cap = { 0 };
    cap.out_r = out_r;
    InitializeCriticalSection(&cap.cs);
    HANDLE hThread = CreateThread(NULL, 0, reader_thread, &cap, 0, NULL);

    /* Wait for authentic cmd.exe startup header + initial prompt (default theme=cmd) */
    ASSERT_TRUE(wait_for_substr(&cap, 0, "Microsoft Windows [", 4000));
    ASSERT_TRUE(wait_for_substr(&cap, 0, "(c) Microsoft Corporation", 4000));
    ASSERT_TRUE(wait_for_substr(&cap, 0, ">", 4000));

    /* Case A: F2 opens Configurator & preserves typed line */
    size_t pos = get_len(&cap);
    send_bytes(in_w, "echo keep_me");
    Sleep(100);
    send_bytes(in_w, "\x1bOQ"); /* F2 */
    ASSERT_TRUE(wait_for_substr(&cap, pos, "Plumbus Configurator", 3000));
    send_bytes(in_w, "\x1b"); /* Esc closes configurator */
    Sleep(150);
    pos = get_len(&cap);
    send_bytes(in_w, "\r");
    ASSERT_TRUE(wait_for_substr(&cap, pos, "keep_me", 3000));

    /* Case B: plumbus config opens Configurator, Space x2 toggles cmd->robbyrussell->agnoster, S saves .cmdrc */
    Sleep(150);
    pos = get_len(&cap);
    send_bytes(in_w, "plumbus config\r");
    ASSERT_TRUE(wait_for_substr(&cap, pos, "Plumbus Configurator", 3000));
    Sleep(100);
    send_bytes(in_w, "  s"); /* Space twice toggles cmd->robbyrussell->agnoster, s saves */
    ASSERT_TRUE(wait_for_substr(&cap, pos, "\xEE\x82\xB0", 3000)); /*  agnoster separator */

    /* Case C: Ghost text appears while typing "echo hel", cleared on Enter! */
    Sleep(150);
    pos = get_len(&cap);
    send_bytes(in_w, "echo hel");
    ASSERT_TRUE(wait_for_substr(&cap, pos, "lo_ghost_tail", 3000));
    size_t before_enter = get_len(&cap);
    send_bytes(in_w, "\r");
    ASSERT_TRUE(wait_for_substr(&cap, before_enter, "hel", 3000));
    Sleep(150);

    EnterCriticalSection(&cap.cs);
    const char *after_enter = cap.buf + before_enter;
    /* Final redraw + output after Enter must NOT show lo_ghost_tail */
    ASSERT_TRUE(strstr(after_enter, "lo_ghost_tail") == NULL);
    LeaveCriticalSection(&cap.cs);

    /* Case D: Switch to omb-box two-line theme and verify multiline redraw doesn't duplicate header */
    Sleep(100);
    pos = get_len(&cap);
    send_bytes(in_w, "theme omb-box\r");
    ASSERT_TRUE(wait_for_substr(&cap, pos, "\xE2\x94\x8F\xE2\x94\x81\xE2\x94\x81", 3000)); /* ┏━━ */
    ASSERT_TRUE(wait_for_substr(&cap, pos, "\xE2\x94\x97\xE2\x94\x81", 3000)); /* ┗━ */
    pos = get_len(&cap);
    send_bytes(in_w, "echo multiline_ok\r");
    ASSERT_TRUE(wait_for_substr(&cap, pos, "multiline_ok", 3000));

    /* Case E: Switch back to classic cmd theme */
    Sleep(100);
    pos = get_len(&cap);
    send_bytes(in_w, "theme cmd\r");
    ASSERT_TRUE(wait_for_substr(&cap, pos, ">", 3000));

    send_bytes(in_w, "exit\r");
    if (WaitForSingleObject(pi.hProcess, 3000) != WAIT_OBJECT_0) {
        TerminateProcess(pi.hProcess, 1);
    }

    ClosePseudoConsole(hpc);
    WaitForSingleObject(hThread, 2000);
    CloseHandle(hThread);
    DeleteCriticalSection(&cap.cs);
    free(cap.buf);
    DeleteProcThreadAttributeList(attr_list);
    free(attr_list);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    CloseHandle(in_w);
    CloseHandle(out_r);

    /* Verify .cmdrc persisted theme=agnoster */
    FILE *frc = _wfopen(rc_file, L"r");
    ASSERT_TRUE(frc != NULL);
    char rcbuf[1024] = { 0 };
    if (frc) { fread(rcbuf, 1, sizeof(rcbuf) - 1, frc); fclose(frc); }
    ASSERT_TRUE(strstr(rcbuf, "theme=agnoster") != NULL);

    DeleteFileW(rc_file);
    DeleteFileW(hist_file);
    SetEnvironmentVariableW(L"PLUMBUS_RC_FILE", NULL);
    SetEnvironmentVariableW(L"PLUMBUS_HISTORY_FILE", NULL);
}

int wmain(void) {
    RUN_TEST(test_tui_f2_plumbus_config_and_ghost_clear);
    TEST_SUMMARY();
}
