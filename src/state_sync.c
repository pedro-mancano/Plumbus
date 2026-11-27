#include "state_sync.h"
#include <windows.h>
#include <stdlib.h>
#include <string.h>

#define SYNC_MAGIC   0x504C5359u /* "PLSY" */
#define SYNC_VERSION 1u

typedef struct {
    uint32_t magic;
    uint32_t version;
    int32_t exit_code;
    uint32_t cwd_wchars; /* includes NUL */
    uint32_t env_wchars; /* includes double-NUL */
} sync_header_t;

static size_t env_block_wchar_len(const wchar_t *block) {
    if (!block) return 0;
    const wchar_t *p = block;
    while (*p) {
        p += wcslen(p) + 1;
    }
    return (size_t)(p - block) + 1; /* includes final '\0' */
}

static bool write_all(HANDLE h, const void *buf, DWORD bytes) {
    const uint8_t *p = (const uint8_t *)buf;
    DWORD rem = bytes;
    while (rem > 0) {
        DWORD written = 0;
        if (!WriteFile(h, p, rem, &written, NULL) || written == 0) return false;
        p += written;
        rem -= written;
    }
    return true;
}

static bool read_all(HANDLE h, void *buf, DWORD bytes) {
    uint8_t *p = (uint8_t *)buf;
    DWORD rem = bytes;
    while (rem > 0) {
        DWORD rd = 0;
        if (!ReadFile(h, p, rem, &rd, NULL) || rd == 0) return false;
        p += rd;
        rem -= rd;
    }
    return true;
}

int state_sync_write_current(const wchar_t *file_path, int exit_code) {
    if (!file_path || !*file_path) return 1;

    DWORD cwd_need = GetCurrentDirectoryW(0, NULL);
    if (cwd_need == 0) return 1;
    wchar_t *cwd = (wchar_t *)malloc((cwd_need + 1) * sizeof(wchar_t));
    if (!cwd) return 1;
    DWORD cwd_got = GetCurrentDirectoryW(cwd_need + 1, cwd);
    if (cwd_got == 0) {
        free(cwd);
        return 1;
    }

    LPWCH env = GetEnvironmentStringsW();
    if (!env) {
        free(cwd);
        return 1;
    }

    size_t cwd_wchars = wcslen(cwd) + 1;
    size_t env_wchars = env_block_wchar_len(env);

    sync_header_t hdr;
    hdr.magic = SYNC_MAGIC;
    hdr.version = SYNC_VERSION;
    hdr.exit_code = (int32_t)exit_code;
    hdr.cwd_wchars = (uint32_t)cwd_wchars;
    hdr.env_wchars = (uint32_t)env_wchars;

    HANDLE h = CreateFileW(file_path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY, NULL);
    bool ok = false;
    if (h != INVALID_HANDLE_VALUE) {
        ok = write_all(h, &hdr, sizeof(hdr)) &&
             write_all(h, cwd, (DWORD)(cwd_wchars * sizeof(wchar_t))) &&
             write_all(h, env, (DWORD)(env_wchars * sizeof(wchar_t)));
        CloseHandle(h);
    }

    FreeEnvironmentStringsW(env);
    free(cwd);
    return ok ? exit_code : 1;
}

bool state_sync_read_file(const wchar_t *file_path, synced_state_t *out_state) {
    if (!file_path || !out_state) return false;
    memset(out_state, 0, sizeof(*out_state));

    HANDLE h = CreateFileW(file_path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return false;

    sync_header_t hdr;
    bool ok = read_all(h, &hdr, sizeof(hdr));
    if (!ok || hdr.magic != SYNC_MAGIC || hdr.version != SYNC_VERSION ||
        hdr.cwd_wchars == 0 || hdr.cwd_wchars > 65536 || hdr.env_wchars == 0 || hdr.env_wchars > (16u * 1024u * 1024u)) {
        CloseHandle(h);
        DeleteFileW(file_path);
        return false;
    }

    out_state->exit_code = hdr.exit_code;
    out_state->cwd = (wchar_t *)malloc(hdr.cwd_wchars * sizeof(wchar_t));
    out_state->env_block = (wchar_t *)malloc(hdr.env_wchars * sizeof(wchar_t));
    out_state->env_block_wchars = hdr.env_wchars;

    if (!out_state->cwd || !out_state->env_block) {
        CloseHandle(h);
        DeleteFileW(file_path);
        state_sync_free(out_state);
        return false;
    }

    ok = read_all(h, out_state->cwd, (DWORD)(hdr.cwd_wchars * sizeof(wchar_t))) &&
         read_all(h, out_state->env_block, (DWORD)(hdr.env_wchars * sizeof(wchar_t)));
    CloseHandle(h);
    DeleteFileW(file_path);

    if (!ok) {
        state_sync_free(out_state);
        return false;
    }

    out_state->cwd[hdr.cwd_wchars - 1] = L'\0';
    out_state->env_block[hdr.env_wchars - 1] = L'\0';
    return true;
}

static bool env_block_contains_name(const wchar_t *block, const wchar_t *name, size_t name_len) {
    for (const wchar_t *p = block; *p; p += wcslen(p) + 1) {
        const wchar_t *eq = wcschr(p + 1, L'=');
        if (!eq) continue;
        size_t cur_len = (size_t)(eq - p);
        if (cur_len == name_len && _wcsnicmp(p, name, name_len) == 0) {
            return true;
        }
    }
    return false;
}

bool state_sync_apply(const synced_state_t *state) {
    if (!state || !state->cwd || !state->env_block) return false;

    /* 1. Snapshot current env to detect removed variables */
    LPWCH cur_env = GetEnvironmentStringsW();
    if (cur_env) {
        for (const wchar_t *p = cur_env; *p; p += wcslen(p) + 1) {
            const wchar_t *eq = wcschr(p + 1, L'=');
            if (!eq) continue;
            size_t nlen = (size_t)(eq - p);
            if (!env_block_contains_name(state->env_block, p, nlen)) {
                wchar_t *name = (wchar_t *)malloc((nlen + 1) * sizeof(wchar_t));
                if (name) {
                    memcpy(name, p, nlen * sizeof(wchar_t));
                    name[nlen] = L'\0';
                    SetEnvironmentVariableW(name, NULL);
                    free(name);
                }
            }
        }
        FreeEnvironmentStringsW(cur_env);
    }

    /* 2. Apply all new/updated variables (skip literal ERRORLEVEL so dynamic %ERRORLEVEL% stays intact) */
    for (const wchar_t *p = state->env_block; *p; p += wcslen(p) + 1) {
        const wchar_t *eq = wcschr(p + 1, L'=');
        if (!eq) continue;
        size_t nlen = (size_t)(eq - p);
        if (nlen == 10 && _wcsnicmp(p, L"ERRORLEVEL", 10) == 0) {
            continue;
        }
        wchar_t *name = (wchar_t *)malloc((nlen + 1) * sizeof(wchar_t));
        if (!name) continue;
        memcpy(name, p, nlen * sizeof(wchar_t));
        name[nlen] = L'\0';
        SetEnvironmentVariableW(name, eq + 1);
        free(name);
    }

    /* 3. Apply working directory */
    if (!SetCurrentDirectoryW(state->cwd)) {
        return false;
    }

    return true;
}

void state_sync_free(synced_state_t *state) {
    if (!state) return;
    free(state->cwd);
    free(state->env_block);
    state->cwd = NULL;
    state->env_block = NULL;
    state->env_block_wchars = 0;
}
