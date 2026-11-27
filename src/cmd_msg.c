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
