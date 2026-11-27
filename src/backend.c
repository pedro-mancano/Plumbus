#include "backend.h"
#include <windows.h>
#include <string.h>

static void normalize_slashes(wchar_t *path) {
    for (wchar_t *p = path; *p; p++) {
        if (*p == L'/') *p = L'\\';
    }
}

bool backend_is_same_file(const wchar_t *path_a, const wchar_t *path_b) {
    if (!path_a || !path_b || !*path_a || !*path_b) return false;

    HANDLE ha = CreateFileW(path_a, 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                            NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
    HANDLE hb = CreateFileW(path_b, 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                            NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);

    if (ha != INVALID_HANDLE_VALUE && hb != INVALID_HANDLE_VALUE) {
        BY_HANDLE_FILE_INFORMATION ia, ib;
        BOOL oka = GetFileInformationByHandle(ha, &ia);
        BOOL okb = GetFileInformationByHandle(hb, &ib);
        CloseHandle(ha);
        CloseHandle(hb);
        if (oka && okb) {
            return ia.dwVolumeSerialNumber == ib.dwVolumeSerialNumber &&
                   ia.nFileIndexHigh == ib.nFileIndexHigh &&
                   ia.nFileIndexLow == ib.nFileIndexLow;
        }
    } else {
        if (ha != INVALID_HANDLE_VALUE) CloseHandle(ha);
        if (hb != INVALID_HANDLE_VALUE) CloseHandle(hb);
    }

    wchar_t fa[MAX_PATH * 4];
    wchar_t fb[MAX_PATH * 4];
    DWORD na = GetFullPathNameW(path_a, (DWORD)(sizeof(fa) / sizeof(fa[0])), fa, NULL);
    DWORD nb = GetFullPathNameW(path_b, (DWORD)(sizeof(fb) / sizeof(fb[0])), fb, NULL);
    if (na > 0 && nb > 0 && na < (sizeof(fa) / sizeof(fa[0])) && nb < (sizeof(fb) / sizeof(fb[0]))) {
        return _wcsicmp(fa, fb) == 0;
    }
    return _wcsicmp(path_a, path_b) == 0;
}

static bool file_exists_w(const wchar_t *path) {
    DWORD attrs = GetFileAttributesW(path);
    return attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_DIRECTORY);
}

backend_status_t backend_resolve_cmd(wchar_t *out_path, size_t out_cap) {
    if (!out_path || out_cap < 16) return BACKEND_ERR_IO;
    out_path[0] = L'\0';

    wchar_t self_path[MAX_PATH * 4];
    DWORD self_len = GetModuleFileNameW(NULL, self_path, (DWORD)(sizeof(self_path) / sizeof(self_path[0])));
    if (self_len == 0 || self_len >= (sizeof(self_path) / sizeof(self_path[0]))) {
        return BACKEND_ERR_IO;
    }

    wchar_t candidate[MAX_PATH * 4];
    candidate[0] = L'\0';

    /* 1. Explicit test/runtime override */
    DWORD env_len = GetEnvironmentVariableW(L"PLUMBUS_CMD_BACKEND", candidate, (DWORD)(sizeof(candidate) / sizeof(candidate[0])));
    if (env_len > 0 && env_len < (sizeof(candidate) / sizeof(candidate[0]))) {
        normalize_slashes(candidate);
        if (!file_exists_w(candidate)) {
            return BACKEND_ERR_NOT_FOUND;
        }
        if (backend_is_same_file(self_path, candidate)) {
            return BACKEND_ERR_SELF_LOOP;
        }
        if (wcslen(candidate) + 1 > out_cap) return BACKEND_ERR_IO;
        wcscpy(out_path, candidate);
        return BACKEND_OK;
    }

    /* 2. Development isolated backup */
#ifdef PLUMBUS_DEV_BACKUP_BACKEND
    wcsncpy(candidate, PLUMBUS_DEV_BACKUP_PATH, (sizeof(candidate) / sizeof(candidate[0])) - 1);
    candidate[(sizeof(candidate) / sizeof(candidate[0])) - 1] = L'\0';
    normalize_slashes(candidate);
    if (file_exists_w(candidate)) {
        if (backend_is_same_file(self_path, candidate)) {
            return BACKEND_ERR_SELF_LOOP;
        }
        if (wcslen(candidate) + 1 > out_cap) return BACKEND_ERR_IO;
        wcscpy(out_path, candidate);
        return BACKEND_OK;
    }
#endif

    /* 3. Production fallback: System32\cmd.exe (read/exec only) */
    UINT sys_len = GetSystemDirectoryW(candidate, (UINT)(sizeof(candidate) / sizeof(candidate[0]) - 16));
    if (sys_len == 0) return BACKEND_ERR_IO;
    if (candidate[sys_len - 1] != L'\\') {
        wcscat(candidate, L"\\");
    }
    wcscat(candidate, L"cmd.exe");

    if (!file_exists_w(candidate)) {
        return BACKEND_ERR_NOT_FOUND;
    }
    if (backend_is_same_file(self_path, candidate)) {
        return BACKEND_ERR_SELF_LOOP;
    }
    if (wcslen(candidate) + 1 > out_cap) return BACKEND_ERR_IO;
    wcscpy(out_path, candidate);
    return BACKEND_OK;
}
