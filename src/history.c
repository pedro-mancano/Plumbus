#include "history.h"
#include <windows.h>
#include <stdlib.h>
#include <string.h>
#include <wctype.h>

static void ensure_parent_dir(const wchar_t *file_path) {
    wchar_t dir[MAX_PATH * 4];
    wcsncpy(dir, file_path, (sizeof(dir) / sizeof(dir[0])) - 1);
    dir[(sizeof(dir) / sizeof(dir[0])) - 1] = L'\0';
    wchar_t *slash = wcsrchr(dir, L'\\');
    if (!slash) slash = wcsrchr(dir, L'/');
    if (slash) {
        *slash = L'\0';
        CreateDirectoryW(dir, NULL);
    }
}

static bool push_entry(history_t *h, const wchar_t *line) {
    if (h->len >= h->max_items && h->max_items > 0) {
        free(h->items[0]);
        memmove(h->items, h->items + 1, (h->len - 1) * sizeof(wchar_t *));
        h->len--;
    }
    if (h->len == h->cap) {
        size_t ncap = h->cap ? h->cap * 2 : 64;
        wchar_t **nb = (wchar_t **)realloc(h->items, ncap * sizeof(wchar_t *));
        if (!nb) return false;
        h->items = nb;
        h->cap = ncap;
    }
    wchar_t *copy = _wcsdup(line);
    if (!copy) return false;
    h->items[h->len++] = copy;
    return true;
}

static void load_history_file(history_t *h) {
    HANDLE f = CreateFileW(h->file_path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE) return;

    LARGE_INTEGER sz;
    if (!GetFileSizeEx(f, &sz) || sz.QuadPart <= 0 || sz.QuadPart > (16LL * 1024 * 1024)) {
        CloseHandle(f);
        return;
    }

    size_t bytes = (size_t)sz.QuadPart;
    char *utf8 = (char *)malloc(bytes + 1);
    if (!utf8) {
        CloseHandle(f);
        return;
    }

    DWORD rd = 0;
    BOOL ok = ReadFile(f, utf8, (DWORD)bytes, &rd, NULL);
    CloseHandle(f);
    if (!ok || rd == 0) {
        free(utf8);
        return;
    }
    utf8[rd] = '\0';

    int wneed = MultiByteToWideChar(CP_UTF8, 0, utf8, (int)rd, NULL, 0);
    if (wneed <= 0) {
        free(utf8);
        return;
    }
    wchar_t *wbuf = (wchar_t *)malloc(((size_t)wneed + 1) * sizeof(wchar_t));
    if (!wbuf) {
        free(utf8);
        return;
    }
    MultiByteToWideChar(CP_UTF8, 0, utf8, (int)rd, wbuf, wneed);
    wbuf[wneed] = L'\0';
    free(utf8);

    wchar_t *p = wbuf;
    while (*p) {
        wchar_t *eol = wcspbrk(p, L"\r\n");
        if (eol) *eol = L'\0';
        if (*p) {
            if (h->len == 0 || wcscmp(h->items[h->len - 1], p) != 0) {
                push_entry(h, p);
            }
        }
        if (!eol) break;
        p = eol + 1;
        while (*p == L'\r' || *p == L'\n') p++;
    }
    free(wbuf);
}

static void append_line_to_file(const wchar_t *file_path, const wchar_t *line) {
    ensure_parent_dir(file_path);
    HANDLE f = CreateFileW(file_path, FILE_APPEND_DATA, FILE_SHARE_READ, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE) return;

    int need = WideCharToMultiByte(CP_UTF8, 0, line, -1, NULL, 0, NULL, NULL);
    if (need > 1) {
        char *buf = (char *)malloc((size_t)need + 2);
        if (buf) {
            WideCharToMultiByte(CP_UTF8, 0, line, -1, buf, need, NULL, NULL);
            size_t slen = strlen(buf);
            buf[slen++] = '\n';
            DWORD wr = 0;
            WriteFile(f, buf, (DWORD)slen, &wr, NULL);
            free(buf);
        }
    }
    CloseHandle(f);
}

bool history_init(history_t *h, size_t max_items) {
    if (!h) return false;
    memset(h, 0, sizeof(*h));
    h->max_items = max_items > 0 ? max_items : 5000;

    DWORD n = GetEnvironmentVariableW(L"PLUMBUS_HISTORY_FILE", h->file_path,
                                      (DWORD)(sizeof(h->file_path) / sizeof(h->file_path[0])));
    if (n == 0 || n >= sizeof(h->file_path) / sizeof(h->file_path[0])) {
        wchar_t appdata[MAX_PATH * 2];
        DWORD ad = GetEnvironmentVariableW(L"LocalAppData", appdata, (DWORD)(sizeof(appdata) / sizeof(appdata[0])));
        if (ad > 0 && ad < sizeof(appdata) / sizeof(appdata[0])) {
            swprintf(h->file_path, sizeof(h->file_path) / sizeof(h->file_path[0]),
                     L"%ls\\plumbus\\history", appdata);
        } else {
            wcscpy(h->file_path, L".plumbus_history");
        }
    }

    load_history_file(h);
    return true;
}

void history_free(history_t *h) {
    if (!h) return;
    for (size_t i = 0; i < h->len; i++) {
        free(h->items[i]);
    }
    free(h->items);
    h->items = NULL;
    h->len = 0;
    h->cap = 0;
}

bool history_add(history_t *h, const wchar_t *line) {
    if (!h || !line) return false;
    const wchar_t *p = line;
    while (*p == L' ' || *p == L'\t') p++;
    if (!*p) return false;

    if (h->len > 0 && wcscmp(h->items[h->len - 1], line) == 0) {
        return true; /* Skip consecutive duplicate */
    }
    if (!push_entry(h, line)) return false;
    append_line_to_file(h->file_path, line);
    return true;
}

const wchar_t *history_get(const history_t *h, size_t index) {
    if (!h || index >= h->len) return NULL;
    return h->items[index];
}

const wchar_t *history_find_ghost_suffix(const history_t *h, const wchar_t *prefix) {
    if (!h || !prefix || !*prefix) return NULL;
    size_t plen = wcslen(prefix);
    for (size_t i = h->len; i > 0; i--) {
        const wchar_t *item = h->items[i - 1];
        if (wcslen(item) > plen && wcsncmp(item, prefix, plen) == 0) {
            return item + plen;
        }
    }
    return NULL;
}

static bool wcsistr_bool(const wchar_t *hay, const wchar_t *needle) {
    if (!*needle) return true;
    size_t nlen = wcslen(needle);
    for (const wchar_t *p = hay; *p; p++) {
        if (_wcsnicmp(p, needle, nlen) == 0) return true;
    }
    return false;
}

ptrdiff_t history_search_reverse(const history_t *h, const wchar_t *query, ptrdiff_t start_idx) {
    if (!h || !query || h->len == 0) return -1;
    ptrdiff_t idx = start_idx;
    if (idx < 0 || (size_t)idx >= h->len) idx = (ptrdiff_t)h->len - 1;

    for (; idx >= 0; idx--) {
        if (wcsistr_bool(h->items[idx], query)) {
            return idx;
        }
    }
    return -1;
}
