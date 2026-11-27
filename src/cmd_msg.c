#include "cmd_msg.h"
#include "builtins.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static HMODULE get_mui_module(void) {
    static bool tried = false;
    static HMODULE mod = NULL;
    if (tried) return mod;
    tried = true;

#if defined(PLUMBUS_DEV_BACKUP_BACKEND) && defined(PLUMBUS_DEV_BACKUP_PATH)
    mod = LoadLibraryExW(PLUMBUS_DEV_BACKUP_PATH, NULL,
                         LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_IMAGE_RESOURCE);
    if (mod) return mod;
#endif
    wchar_t sys_dir[MAX_PATH];
    UINT n = GetSystemDirectoryW(sys_dir, MAX_PATH);
    if (n > 0 && n + 10 < MAX_PATH) {
        wcscat(sys_dir, L"\\cmd.exe");
        mod = LoadLibraryExW(sys_dir, NULL,
                             LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_IMAGE_RESOURCE);
    }
    return mod;
}

void cmd_msg_win32(HANDLE h, DWORD win32_err) {
    if (!h || h == INVALID_HANDLE_VALUE) return;
    wchar_t *buf = NULL;
    DWORD n = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        NULL, win32_err, 0, (LPWSTR)&buf, 0, NULL);
    if (n > 0 && buf) {
        cmd_io_write_w(h, buf, (size_t)n);
        LocalFree(buf);
        return;
    }
    cmd_io_printf(h, L"Error %lu\r\n", (unsigned long)win32_err);
}

void cmd_msg_format_buf(DWORD msg_id, const wchar_t *fallback,
                        const wchar_t *arg1, const wchar_t *arg2,
                        wchar_t *out_buf, size_t out_cap) {
    if (!out_buf || out_cap == 0) return;
    out_buf[0] = L'\0';

    HMODULE mod = get_mui_module();
    if (mod) {
        wchar_t *buf = NULL;
        DWORD flags = FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_HMODULE;
        DWORD_PTR args[2] = { (DWORD_PTR)(arg1 ? arg1 : L""), (DWORD_PTR)(arg2 ? arg2 : L"") };
        DWORD n = 0;
        if (arg1 || arg2) {
            flags |= FORMAT_MESSAGE_ARGUMENT_ARRAY;
            n = FormatMessageW(flags, mod, msg_id, 0, (LPWSTR)&buf, 0, (va_list *)args);
        } else {
            flags |= FORMAT_MESSAGE_IGNORE_INSERTS;
            n = FormatMessageW(flags, mod, msg_id, 0, (LPWSTR)&buf, 0, NULL);
        }
        if (n > 0 && buf) {
            wcsncpy(out_buf, buf, out_cap - 1);
            out_buf[out_cap - 1] = L'\0';
            LocalFree(buf);
            return;
        }
    }

    if (!fallback) return;
    /* Simple %1 / %2 substitution in fallback */
    size_t oi = 0;
    for (const wchar_t *p = fallback; *p && oi + 1 < out_cap; p++) {
        if (*p == L'%' && (p[1] == L'1' || p[1] == L'2')) {
            const wchar_t *ins = (p[1] == L'1') ? (arg1 ? arg1 : L"") : (arg2 ? arg2 : L"");
            size_t ilen = wcslen(ins);
            if (oi + ilen >= out_cap) ilen = out_cap - 1 - oi;
            memcpy(out_buf + oi, ins, ilen * sizeof(wchar_t));
            oi += ilen;
            p++;
        } else {
            out_buf[oi++] = *p;
        }
    }
    out_buf[oi] = L'\0';
}

void cmd_msg_print(HANDLE h, DWORD msg_id, const wchar_t *fallback,
                   const wchar_t *arg1, const wchar_t *arg2) {
    if (!h || h == INVALID_HANDLE_VALUE) return;
    wchar_t buf[4096];
    cmd_msg_format_buf(msg_id, fallback, arg1, arg2, buf, sizeof(buf) / sizeof(buf[0]));
    cmd_io_write_sz(h, buf);
}

typedef LONG (WINAPI *cmd_rtl_get_version_fn)(OSVERSIONINFOW *);
typedef LPWSTR (WINAPI *cmd_branding_fmt_fn)(LPCWSTR);

void cmd_format_ver_banner(wchar_t *out_buf, size_t out_cap) {
    if (!out_buf || out_cap == 0) return;
    out_buf[0] = L'\0';

    OSVERSIONINFOW osv;
    ZeroMemory(&osv, sizeof(osv));
    osv.dwOSVersionInfoSize = sizeof(osv);
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    if (ntdll) {
        cmd_rtl_get_version_fn fn = (cmd_rtl_get_version_fn)(void *)GetProcAddress(ntdll, "RtlGetVersion");
        if (fn) fn(&osv);
    }

    DWORD ubr = 0;
    DWORD cb = sizeof(ubr);
    wchar_t ver[64];
    if (RegGetValueW(HKEY_LOCAL_MACHINE,
                     L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion",
                     L"UBR", RRF_RT_REG_DWORD, NULL, &ubr, &cb) == ERROR_SUCCESS) {
        swprintf(ver, sizeof(ver) / sizeof(ver[0]), L"%lu.%lu.%05lu.%lu",
                 (unsigned long)osv.dwMajorVersion,
                 (unsigned long)osv.dwMinorVersion,
                 (unsigned long)osv.dwBuildNumber,
                 (unsigned long)ubr);
    } else {
        swprintf(ver, sizeof(ver) / sizeof(ver[0]), L"%lu.%lu.%lu",
                 (unsigned long)osv.dwMajorVersion,
                 (unsigned long)osv.dwMinorVersion,
                 (unsigned long)osv.dwBuildNumber);
    }

    cmd_msg_format_buf(CMD_MSG_VER_BANNER, L"Microsoft Windows [Version %1]",
                       ver, NULL, out_buf, out_cap);
    size_t len = wcslen(out_buf);
    while (len > 0 && (out_buf[len - 1] == L'\r' || out_buf[len - 1] == L'\n')) {
        out_buf[--len] = L'\0';
    }
}

void cmd_print_startup_header(HANDLE h) {
    if (!h || h == INVALID_HANDLE_VALUE) return;

    wchar_t line1[256];
    cmd_format_ver_banner(line1, sizeof(line1) / sizeof(line1[0]));
    cmd_io_write_sz(h, line1);
    cmd_io_write_sz(h, L"\r\n");

    bool printed_copyright = false;
    HMODULE wb = LoadLibraryW(L"winbrand.dll");
    if (wb) {
        cmd_branding_fmt_fn fn = (cmd_branding_fmt_fn)(void *)GetProcAddress(wb, "BrandingFormatString");
        if (fn) {
            LPWSTR br = fn(L"%WINDOWS_COPYRIGHT%");
            if (br && br[0]) {
                if (br[0] == L'\u00A9') {
                    cmd_io_write_sz(h, L"(c)");
                    cmd_io_write_sz(h, br + 1);
                } else {
                    cmd_io_write_sz(h, br);
                }
                cmd_io_write_sz(h, L"\r\n");
                printed_copyright = true;
            }
            if (br) GlobalFree(br);
        }
        FreeLibrary(wb);
    }

    if (!printed_copyright) {
        cmd_io_write_sz(h, L"(c) Microsoft Corporation. All rights reserved.\r\n");
    }
    cmd_io_write_sz(h, L"\r\n");
}
