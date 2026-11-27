#include "proxy.h"
#include <windows.h>
#include <stdlib.h>
#include <string.h>

bool proxy_is_stdin_interactive(void) {
    HANDLE hIn = GetStdHandle(STD_INPUT_HANDLE);
    if (hIn == INVALID_HANDLE_VALUE || hIn == NULL) return false;
    if (GetFileType(hIn) != FILE_TYPE_CHAR) return false;
    DWORD mode = 0;
    return GetConsoleMode(hIn, &mode) != 0;
}

int proxy_delegate_raw(const wchar_t *backend_cmd_path, const wchar_t *raw_after_exe) {
    if (!backend_cmd_path || !*backend_cmd_path) return 1;
    const wchar_t *tail = raw_after_exe ? raw_after_exe : L"";

    size_t blen = wcslen(backend_cmd_path);
    size_t tlen = wcslen(tail);
    wchar_t *cmdline = (wchar_t *)malloc((blen + 2 + tlen + 1) * sizeof(wchar_t));
    if (!cmdline) return 1;

    cmdline[0] = L'"';
    memcpy(cmdline + 1, backend_cmd_path, blen * sizeof(wchar_t));
    cmdline[1 + blen] = L'"';
    if (tlen > 0) {
        memcpy(cmdline + 2 + blen, tail, tlen * sizeof(wchar_t));
    }
    cmdline[2 + blen + tlen] = L'\0';

    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof(si));
    ZeroMemory(&pi, sizeof(pi));
    si.cb = sizeof(si);

    /* Ignore Ctrl+C in parent while child process owns console */
    SetConsoleCtrlHandler(NULL, TRUE);

    BOOL ok = CreateProcessW(
        backend_cmd_path,
        cmdline,
        NULL,
        NULL,
        TRUE,
        0,
        NULL,
        NULL,
        &si,
        &pi
    );

    free(cmdline);

    if (!ok) {
        SetConsoleCtrlHandler(NULL, FALSE);
        return (int)GetLastError();
    }

    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD exit_code = 1;
    GetExitCodeProcess(pi.hProcess, &exit_code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    SetConsoleCtrlHandler(NULL, FALSE);
    return (int)exit_code;
}
