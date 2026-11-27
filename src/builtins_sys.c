#include "builtins.h"
#include "resolver.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wctype.h>

int builtin_echo(shell_ctx_t *ctx, const wchar_t *tail, const cmd_io_t *io) {
    if (!tail || !*tail) {
        cmd_io_printf(io->h_out, L"ECHO is %ls.\r\n", (ctx && ctx->echo_off) ? L"off" : L"on");
        return 0;
    }
    wchar_t first = tail[0];
    if (first == L'.' || first == L':' || first == L'/' || first == L'+' ||
        first == L',' || first == L';' || first == L'=' || first == L'(') {
        cmd_io_write_sz(io->h_out, tail + 1);
        cmd_io_write_sz(io->h_out, L"\r\n");
        return 0;
    }

    const wchar_t *trimmed = cmd_skip_ws(tail);
    if (_wcsicmp(trimmed, L"on") == 0) {
        if (ctx) ctx->echo_off = false;
        return 0;
    }
    if (_wcsicmp(trimmed, L"off") == 0) {
        if (ctx) ctx->echo_off = true;
        return 0;
    }

    const wchar_t *msg = (tail[0] == L' ' || tail[0] == L'\t') ? (tail + 1) : tail;
    cmd_io_write_sz(io->h_out, msg);
    cmd_io_write_sz(io->h_out, L"\r\n");
    return 0;
}

int builtin_cls(shell_ctx_t *ctx, const wchar_t *tail, const cmd_io_t *io) {
    (void)ctx;
    (void)tail;
    DWORD mode = 0;
    if (GetConsoleMode(io->h_out, &mode)) {
        CONSOLE_SCREEN_BUFFER_INFO csbi;
        if (GetConsoleScreenBufferInfo(io->h_out, &csbi)) {
            DWORD cells = (DWORD)csbi.dwSize.X * (DWORD)csbi.dwSize.Y;
            COORD origin = { 0, 0 };
            DWORD written = 0;
            FillConsoleOutputCharacterW(io->h_out, L' ', cells, origin, &written);
            FillConsoleOutputAttribute(io->h_out, csbi.wAttributes, cells, origin, &written);
            SetConsoleCursorPosition(io->h_out, origin);
            return 0;
        }
    }
    cmd_io_write_sz(io->h_out, L"\x1b[2J\x1b[H");
    return 0;
}

int builtin_title(shell_ctx_t *ctx, const wchar_t *tail, const cmd_io_t *io) {
    (void)ctx;
    (void)io;
    const wchar_t *p = cmd_skip_ws(tail);
    SetConsoleTitleW(p);
    return 0;
}

static int hex_val(wchar_t c) {
    if (c >= L'0' && c <= L'9') return c - L'0';
    if (c >= L'a' && c <= L'f') return 10 + (c - L'a');
    if (c >= L'A' && c <= L'F') return 10 + (c - L'A');
    return -1;
}

int builtin_color(shell_ctx_t *ctx, const wchar_t *tail, const cmd_io_t *io) {
    (void)ctx;
    wchar_t arg[32];
    wcsncpy(arg, cmd_skip_ws(tail), 31);
    arg[31] = L'\0';
    cmd_trim_trailing_ws(arg);

    WORD attr = 0x07;
    if (arg[0]) {
        size_t len = wcslen(arg);
        if (len > 2) return 1;
        if (len == 1) {
            int fg = hex_val(arg[0]);
            if (fg < 0) return 1;
            attr = (WORD)fg;
        } else {
            int bg = hex_val(arg[0]);
            int fg = hex_val(arg[1]);
            if (bg < 0 || fg < 0 || bg == fg) return 1;
            attr = (WORD)((bg << 4) | fg);
        }
    }
    DWORD mode = 0;
    if (GetConsoleMode(io->h_out, &mode)) {
        SetConsoleTextAttribute(io->h_out, attr);
        CONSOLE_SCREEN_BUFFER_INFO csbi;
        if (GetConsoleScreenBufferInfo(io->h_out, &csbi)) {
            DWORD cells = (DWORD)csbi.dwSize.X * (DWORD)csbi.dwSize.Y;
            COORD origin = { 0, 0 };
            DWORD written = 0;
            FillConsoleOutputAttribute(io->h_out, attr, cells, origin, &written);
        }
    }
    return 0;
}

typedef LONG (WINAPI *rtl_get_version_fn)(OSVERSIONINFOW *);

int builtin_ver(shell_ctx_t *ctx, const wchar_t *tail, const cmd_io_t *io) {
    (void)ctx;
    (void)tail;
    OSVERSIONINFOW osv;
    ZeroMemory(&osv, sizeof(osv));
    osv.dwOSVersionInfoSize = sizeof(osv);
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    if (ntdll) {
        rtl_get_version_fn fn = (rtl_get_version_fn)(void *)GetProcAddress(ntdll, "RtlGetVersion");
        if (fn) fn(&osv);
    }
    cmd_io_printf(io->h_out, L"\r\nMicrosoft Windows [Version %lu.%lu.%lu]\r\n",
                  (unsigned long)osv.dwMajorVersion,
                  (unsigned long)osv.dwMinorVersion,
                  (unsigned long)osv.dwBuildNumber);
    return 0;
}

int builtin_vol(shell_ctx_t *ctx, const wchar_t *tail, const cmd_io_t *io) {
    (void)ctx;
    wchar_t root[8] = L"C:\\";
    const wchar_t *p = cmd_skip_ws(tail);
    if (iswalpha(p[0]) && p[1] == L':') {
        root[0] = towupper(p[0]);
    } else {
        wchar_t cwd[MAX_PATH];
        if (GetCurrentDirectoryW(MAX_PATH, cwd) >= 2 && cwd[1] == L':') {
            root[0] = towupper(cwd[0]);
        }
    }
    wchar_t label[MAX_PATH] = { 0 };
    DWORD serial = 0;
    if (!GetVolumeInformationW(root, label, MAX_PATH, &serial, NULL, NULL, NULL, 0)) {
        cmd_io_write_sz(io->h_err, L"The system cannot find the drive specified.\r\n");
        return 1;
    }
    if (label[0]) {
        cmd_io_printf(io->h_out, L" Volume in drive %lc is %ls\r\n", root[0], label);
    } else {
        cmd_io_printf(io->h_out, L" Volume in drive %lc has no label.\r\n", root[0]);
    }
    cmd_io_printf(io->h_out, L" Volume Serial Number is %04X-%04X\r\n",
                  (unsigned)((serial >> 16) & 0xFFFFu), (unsigned)(serial & 0xFFFFu));
    return 0;
}

int builtin_date(shell_ctx_t *ctx, const wchar_t *tail, const cmd_io_t *io) {
    (void)ctx;
    const wchar_t *p = cmd_skip_ws(tail);
    wchar_t buf[128];
    if (GetDateFormatW(LOCALE_USER_DEFAULT, DATE_SHORTDATE, NULL, NULL, buf, 128) == 0) {
        return 1;
    }
    if (_wcsnicmp(p, L"/t", 2) == 0) {
        cmd_io_printf(io->h_out, L"%ls\r\n", buf);
        return 0;
    }
    cmd_io_printf(io->h_out, L"The current date is: %ls\r\n", buf);
    return 0;
}

int builtin_time(shell_ctx_t *ctx, const wchar_t *tail, const cmd_io_t *io) {
    (void)ctx;
    const wchar_t *p = cmd_skip_ws(tail);
    SYSTEMTIME st;
    GetLocalTime(&st);
    if (_wcsnicmp(p, L"/t", 2) == 0) {
        cmd_io_printf(io->h_out, L"%02u:%02u\r\n", (unsigned)st.wHour, (unsigned)st.wMinute);
        return 0;
    }
    cmd_io_printf(io->h_out, L"The current time is: %2u:%02u:%02u.%02u\r\n",
                  (unsigned)st.wHour, (unsigned)st.wMinute,
                  (unsigned)st.wSecond, (unsigned)(st.wMilliseconds / 10));
    return 0;
}

int builtin_pause(shell_ctx_t *ctx, const wchar_t *tail, const cmd_io_t *io) {
    (void)ctx;
    (void)tail;
    cmd_io_write_sz(io->h_out, L"Press any key to continue . . . ");
    DWORD mode = 0;
    if (GetConsoleMode(io->h_in, &mode)) {
        INPUT_RECORD rec;
        DWORD rd = 0;
        while (ReadConsoleInputW(io->h_in, &rec, 1, &rd) && rd == 1) {
            if (rec.EventType == KEY_EVENT && rec.Event.KeyEvent.bKeyDown &&
                rec.Event.KeyEvent.wVirtualKeyCode != VK_SHIFT &&
                rec.Event.KeyEvent.wVirtualKeyCode != VK_CONTROL &&
                rec.Event.KeyEvent.wVirtualKeyCode != VK_MENU) {
                break;
            }
        }
    } else {
        char ch = 0;
        DWORD rd = 0;
        ReadFile(io->h_in, &ch, 1, &rd, NULL);
    }
    cmd_io_write_sz(io->h_out, L"\r\n");
    return 0;
}

static int toggle_flag_cmd(bool *flag, const wchar_t *label, const wchar_t *tail, const cmd_io_t *io) {
    wchar_t arg[32];
    wcsncpy(arg, cmd_skip_ws(tail), 31);
    arg[31] = L'\0';
    cmd_trim_trailing_ws(arg);
    if (!arg[0]) {
        cmd_io_printf(io->h_out, L"%ls is %ls.\r\n", label, *flag ? L"on" : L"off");
        return 0;
    }
    if (_wcsicmp(arg, L"on") == 0) { *flag = true; return 0; }
    if (_wcsicmp(arg, L"off") == 0) { *flag = false; return 0; }
    cmd_io_write_sz(io->h_err, L"Must specify ON or OFF.\r\n");
    return 1;
}

int builtin_verify(shell_ctx_t *ctx, const wchar_t *tail, const cmd_io_t *io) {
    return toggle_flag_cmd(&ctx->verify_on, L"VERIFY", tail, io);
}

int builtin_keys(shell_ctx_t *ctx, const wchar_t *tail, const cmd_io_t *io) {
    return toggle_flag_cmd(&ctx->keys_on, L"KEYS", tail, io);
}

int builtin_break(shell_ctx_t *ctx, const wchar_t *tail, const cmd_io_t *io) {
    (void)ctx;
    (void)tail;
    (void)io;
    return 0;
}

int builtin_start(shell_ctx_t *ctx, const wchar_t *tail, const cmd_io_t *io) {
    (void)ctx;
    const wchar_t *p = cmd_skip_ws(tail);
    wchar_t title[256] = { 0 };
    wchar_t work_dir[MAX_PATH * 4] = { 0 };
    bool wait = false;
    bool bg = false;
    WORD show_cmd = SW_SHOWNORMAL;

    /* Optional "title" */
    if (*p == L'"') {
        p++;
        size_t ti = 0;
        while (*p && *p != L'"') {
            if (ti + 1 < 256) title[ti++] = *p;
            p++;
        }
        title[ti] = L'\0';
        if (*p == L'"') p++;
        p = cmd_skip_ws(p);
    }

    while (*p == L'/') {
        if (_wcsnicmp(p, L"/b", 2) == 0 && (p[2] == L'\0' || iswspace(p[2]))) {
            bg = true; p = cmd_skip_ws(p + 2);
        } else if (_wcsnicmp(p, L"/wait", 5) == 0 && (p[5] == L'\0' || iswspace(p[5]))) {
            wait = true; p = cmd_skip_ws(p + 5);
        } else if (_wcsnicmp(p, L"/w", 2) == 0 && (p[2] == L'\0' || iswspace(p[2]))) {
            wait = true; p = cmd_skip_ws(p + 2);
        } else if (_wcsnicmp(p, L"/min", 4) == 0 && (p[4] == L'\0' || iswspace(p[4]))) {
            show_cmd = SW_SHOWMINIMIZED; p = cmd_skip_ws(p + 4);
        } else if (_wcsnicmp(p, L"/max", 4) == 0 && (p[4] == L'\0' || iswspace(p[4]))) {
            show_cmd = SW_SHOWMAXIMIZED; p = cmd_skip_ws(p + 4);
        } else if (_wcsnicmp(p, L"/d", 2) == 0 && iswspace(p[2])) {
            p = cmd_skip_ws(p + 2);
            size_t di = 0;
            bool q = (*p == L'"');
            if (q) p++;
            while (*p && (q ? *p != L'"' : !iswspace(*p))) {
                if (di + 1 < sizeof(work_dir) / sizeof(work_dir[0])) work_dir[di++] = *p;
                p++;
            }
            work_dir[di] = L'\0';
            if (q && *p == L'"') p++;
            p = cmd_skip_ws(p);
        } else {
            while (*p && !iswspace(*p)) p++;
            p = cmd_skip_ws(p);
        }
    }

    wchar_t cmdline[8192];
    if (!*p) {
        swprintf(cmdline, 8192, L"\"%ls\"", ctx->self_path);
    } else {
        wcsncpy(cmdline, p, 8191);
        cmdline[8191] = L'\0';
    }

    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof(si));
    ZeroMemory(&pi, sizeof(pi));
    si.cb = sizeof(si);
    if (title[0]) si.lpTitle = title;
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = show_cmd;

    DWORD flags = bg ? 0 : CREATE_NEW_CONSOLE;
    BOOL ok = CreateProcessW(NULL, cmdline, NULL, NULL, bg ? TRUE : FALSE,
                             flags, NULL, work_dir[0] ? work_dir : NULL, &si, &pi);
    if (!ok) {
        cmd_io_printf(io->h_err, L"The system cannot find the file %ls.\r\n", p);
        return 1;
    }
    DWORD ec = 0;
    if (wait) {
        WaitForSingleObject(pi.hProcess, INFINITE);
        GetExitCodeProcess(pi.hProcess, &ec);
    }
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return (int)ec;
}

int builtin_assoc(shell_ctx_t *ctx, const wchar_t *tail, const cmd_io_t *io) {
    (void)ctx;
    wchar_t arg[512];
    wcsncpy(arg, cmd_skip_ws(tail), 511);
    arg[511] = L'\0';
    cmd_trim_trailing_ws(arg);

    if (!arg[0]) {
        DWORD idx = 0;
        wchar_t name[256];
        DWORD nlen = 256;
        while (RegEnumKeyExW(HKEY_CLASSES_ROOT, idx++, name, &nlen, NULL, NULL, NULL, NULL) == ERROR_SUCCESS) {
            if (name[0] == L'.') {
                wchar_t val[256];
                DWORD vlen = sizeof(val);
                if (RegGetValueW(HKEY_CLASSES_ROOT, name, NULL, RRF_RT_REG_SZ, NULL, val, &vlen) == ERROR_SUCCESS && val[0]) {
                    cmd_io_printf(io->h_out, L"%ls=%ls\r\n", name, val);
                }
            }
            nlen = 256;
        }
        return 0;
    }

    wchar_t *eq = wcschr(arg, L'=');
    if (!eq) {
        wchar_t val[256];
        DWORD vlen = sizeof(val);
        if (RegGetValueW(HKEY_CLASSES_ROOT, arg, NULL, RRF_RT_REG_SZ, NULL, val, &vlen) == ERROR_SUCCESS && val[0]) {
            cmd_io_printf(io->h_out, L"%ls=%ls\r\n", arg, val);
            return 0;
        }
        cmd_io_printf(io->h_err, L"File association not found for extension %ls\r\n", arg);
        return 1;
    }
    *eq = L'\0';
    const wchar_t *rhs = eq + 1;
    HKEY hk = NULL;
    if (RegCreateKeyExW(HKEY_CLASSES_ROOT, arg, 0, NULL, 0, KEY_SET_VALUE, NULL, &hk, NULL) != ERROR_SUCCESS) {
        cmd_io_write_sz(io->h_err, L"Access is denied.\r\n");
        return 1;
    }
    LSTATUS st = rhs[0]
        ? RegSetValueExW(hk, NULL, 0, REG_SZ, (const BYTE *)rhs, (DWORD)((wcslen(rhs) + 1) * sizeof(wchar_t)))
        : RegDeleteValueW(hk, NULL);
    RegCloseKey(hk);
    return (st == ERROR_SUCCESS) ? 0 : 1;
}

int builtin_ftype(shell_ctx_t *ctx, const wchar_t *tail, const cmd_io_t *io) {
    (void)ctx;
    wchar_t arg[512];
    wcsncpy(arg, cmd_skip_ws(tail), 511);
    arg[511] = L'\0';
    cmd_trim_trailing_ws(arg);

    if (!arg[0]) {
        DWORD idx = 0;
        wchar_t name[256];
        DWORD nlen = 256;
        while (RegEnumKeyExW(HKEY_CLASSES_ROOT, idx++, name, &nlen, NULL, NULL, NULL, NULL) == ERROR_SUCCESS) {
            if (name[0] != L'.') {
                wchar_t sub[320];
                swprintf(sub, 320, L"%ls\\shell\\open\\command", name);
                wchar_t val[1024];
                DWORD vlen = sizeof(val);
                if (RegGetValueW(HKEY_CLASSES_ROOT, sub, NULL, RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ, NULL, val, &vlen) == ERROR_SUCCESS && val[0]) {
                    cmd_io_printf(io->h_out, L"%ls=%ls\r\n", name, val);
                }
            }
            nlen = 256;
        }
        return 0;
    }

    wchar_t *eq = wcschr(arg, L'=');
    if (!eq) {
        wchar_t sub[320];
        swprintf(sub, 320, L"%ls\\shell\\open\\command", arg);
        wchar_t val[1024];
        DWORD vlen = sizeof(val);
        if (RegGetValueW(HKEY_CLASSES_ROOT, sub, NULL, RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ, NULL, val, &vlen) == ERROR_SUCCESS && val[0]) {
            cmd_io_printf(io->h_out, L"%ls=%ls\r\n", arg, val);
            return 0;
        }
        cmd_io_printf(io->h_err, L"File type '%ls' not found or no open command associated with it.\r\n", arg);
        return 1;
    }
    *eq = L'\0';
    const wchar_t *rhs = eq + 1;
    wchar_t sub[320];
    swprintf(sub, 320, L"%ls\\shell\\open\\command", arg);
    HKEY hk = NULL;
    if (RegCreateKeyExW(HKEY_CLASSES_ROOT, sub, 0, NULL, 0, KEY_SET_VALUE, NULL, &hk, NULL) != ERROR_SUCCESS) {
        cmd_io_write_sz(io->h_err, L"Access is denied.\r\n");
        return 1;
    }
    LSTATUS st = rhs[0]
        ? RegSetValueExW(hk, NULL, 0, REG_EXPAND_SZ, (const BYTE *)rhs, (DWORD)((wcslen(rhs) + 1) * sizeof(wchar_t)))
        : RegDeleteValueW(hk, NULL);
    RegCloseKey(hk);
    return (st == ERROR_SUCCESS) ? 0 : 1;
}
