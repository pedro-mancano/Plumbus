#include "autorun.h"
#include <windows.h>
#include <stdlib.h>
#include <string.h>

static wchar_t *read_autorun_from_hive(HKEY root) {
    HKEY hKey = NULL;
    LONG rc = RegOpenKeyExW(root, L"Software\\Microsoft\\Command Processor", 0, KEY_READ, &hKey);
    if (rc != ERROR_SUCCESS) return NULL;

    DWORD type = 0;
    DWORD bytes = 0;
    rc = RegQueryValueExW(hKey, L"AutoRun", NULL, &type, NULL, &bytes);
    if (rc != ERROR_SUCCESS || bytes == 0 || (type != REG_SZ && type != REG_EXPAND_SZ)) {
        RegCloseKey(hKey);
        return NULL;
    }

    wchar_t *raw = (wchar_t *)malloc(bytes + sizeof(wchar_t));
    if (!raw) {
        RegCloseKey(hKey);
        return NULL;
    }

    rc = RegQueryValueExW(hKey, L"AutoRun", NULL, &type, (LPBYTE)raw, &bytes);
    RegCloseKey(hKey);
    if (rc != ERROR_SUCCESS) {
        free(raw);
        return NULL;
    }
    raw[bytes / sizeof(wchar_t)] = L'\0';

    if (raw[0] == L'\0') {
        free(raw);
        return NULL;
    }

    if (type == REG_EXPAND_SZ) {
        DWORD need = ExpandEnvironmentStringsW(raw, NULL, 0);
        if (need > 0) {
            wchar_t *expanded = (wchar_t *)malloc(need * sizeof(wchar_t));
            if (expanded) {
                if (ExpandEnvironmentStringsW(raw, expanded, need) > 0) {
                    free(raw);
                    return expanded;
                }
                free(expanded);
            }
        }
    }

    return raw;
}

wchar_t *autorun_read_combined(bool disable_autorun) {
    if (disable_autorun) return NULL;

    wchar_t *hklm = read_autorun_from_hive(HKEY_LOCAL_MACHINE);
    wchar_t *hkcu = read_autorun_from_hive(HKEY_CURRENT_USER);

    if (!hklm && !hkcu) return NULL;
    if (hklm && !hkcu) return hklm;
    if (!hklm && hkcu) return hkcu;

    size_t len = wcslen(hklm) + 3 + wcslen(hkcu) + 1;
    wchar_t *combined = (wchar_t *)malloc(len * sizeof(wchar_t));
    if (!combined) {
        free(hklm);
        free(hkcu);
        return NULL;
    }
    wcscpy(combined, hklm);
    wcscat(combined, L" & ");
    wcscat(combined, hkcu);
    free(hklm);
    free(hkcu);
    return combined;
}
