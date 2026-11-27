#include "git_native.h"
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t read_be32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8)  | ((uint32_t)p[3]);
}

static uint16_t read_be16(const uint8_t *p) {
    return (uint16_t)(((uint16_t)p[0] << 8) | ((uint16_t)p[1]));
}

static uint32_t fnv1a_hash(const char *s) {
    uint32_t h = 2166136261u;
    for (; *s; s++) {
        h ^= (uint8_t)*s;
        h *= 16777619u;
    }
    return h;
}

typedef struct {
    char *path; /* UTF-8 forward-slash relative path */
    uint32_t mtime_s;
    uint32_t size;
} idx_entry_t;

typedef struct {
    idx_entry_t *slots;
    size_t cap; /* power of 2 */
    size_t count;
} idx_table_t;

static bool idx_table_init(idx_table_t *t, size_t expected) {
    size_t cap = 64;
    while (cap < expected * 2) cap *= 2;
    t->slots = (idx_entry_t *)calloc(cap, sizeof(idx_entry_t));
    if (!t->slots) return false;
    t->cap = cap;
    t->count = 0;
    return true;
}

static void idx_table_free(idx_table_t *t) {
    if (!t || !t->slots) return;
    for (size_t i = 0; i < t->cap; i++) {
        free(t->slots[i].path);
    }
    free(t->slots);
    t->slots = NULL;
    t->cap = 0;
    t->count = 0;
}

static bool idx_table_insert(idx_table_t *t, const char *path, uint32_t mtime_s, uint32_t size) {
    if ((t->count + 1) * 2 >= t->cap) {
        size_t ncap = t->cap * 2;
        idx_entry_t *ns = (idx_entry_t *)calloc(ncap, sizeof(idx_entry_t));
        if (!ns) return false;
        for (size_t i = 0; i < t->cap; i++) {
            if (t->slots[i].path) {
                uint32_t idx = fnv1a_hash(t->slots[i].path) & (uint32_t)(ncap - 1);
                while (ns[idx].path) idx = (idx + 1) & (uint32_t)(ncap - 1);
                ns[idx] = t->slots[i];
            }
        }
        free(t->slots);
        t->slots = ns;
        t->cap = ncap;
    }

    uint32_t idx = fnv1a_hash(path) & (uint32_t)(t->cap - 1);
    while (t->slots[idx].path) {
        if (strcmp(t->slots[idx].path, path) == 0) {
            t->slots[idx].mtime_s = mtime_s;
            t->slots[idx].size = size;
            return true;
        }
        idx = (idx + 1) & (uint32_t)(t->cap - 1);
    }
    t->slots[idx].path = _strdup(path);
    t->slots[idx].mtime_s = mtime_s;
    t->slots[idx].size = size;
    t->count++;
    return t->slots[idx].path != NULL;
}

static bool idx_table_contains(const idx_table_t *t, const char *path) {
    if (!t || !t->slots || t->cap == 0) return false;
    uint32_t idx = fnv1a_hash(path) & (uint32_t)(t->cap - 1);
    while (t->slots[idx].path) {
        if (strcmp(t->slots[idx].path, path) == 0) return true;
        idx = (idx + 1) & (uint32_t)(t->cap - 1);
    }
    return false;
}

typedef struct {
    char *pattern;
    bool dir_only;
    bool anchored;
} ignore_rule_t;

typedef struct {
    ignore_rule_t *rules;
    size_t count;
    size_t cap;
} ignore_list_t;

static void ignore_list_free(ignore_list_t *il) {
    if (!il) return;
    for (size_t i = 0; i < il->count; i++) free(il->rules[i].pattern);
    free(il->rules);
    il->rules = NULL;
    il->count = 0;
    il->cap = 0;
}

static bool glob_match(const char *pat, const char *str) {
    while (*pat) {
        if (*pat == '*') {
            while (*pat == '*') pat++;
            if (!*pat) return true;
            for (; *str; str++) {
                if (glob_match(pat, str)) return true;
            }
            return false;
        } else if (*pat == '?') {
            if (!*str) return false;
            pat++;
            str++;
        } else {
            if (*pat != *str) return false;
            pat++;
            str++;
        }
    }
    return *str == '\0';
}

static void load_ignore_file(ignore_list_t *il, const wchar_t *file_path) {
    HANDLE f = CreateFileW(file_path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE) return;
    LARGE_INTEGER sz;
    if (!GetFileSizeEx(f, &sz) || sz.QuadPart <= 0 || sz.QuadPart > (1024 * 1024)) {
        CloseHandle(f);
        return;
    }
    char *buf = (char *)malloc((size_t)sz.QuadPart + 1);
    if (!buf) { CloseHandle(f); return; }
    DWORD rd = 0;
    BOOL ok = ReadFile(f, buf, (DWORD)sz.QuadPart, &rd, NULL);
    CloseHandle(f);
    if (!ok) { free(buf); return; }
    buf[rd] = '\0';

    char *p = buf;
    while (*p) {
        char *eol = strpbrk(p, "\r\n");
        if (eol) *eol = '\0';
        while (*p == ' ' || *p == '\t') p++;
        size_t len = strlen(p);
        while (len > 0 && (p[len - 1] == ' ' || p[len - 1] == '\t')) p[--len] = '\0';

        if (len > 0 && p[0] != '#' && p[0] != '!') {
            bool anchored = (p[0] == '/');
            if (anchored) { p++; len--; }
            bool dir_only = (len > 0 && p[len - 1] == '/');
            if (dir_only) { p[--len] = '\0'; }
            if (len > 0) {
                if (il->count == il->cap) {
                    size_t ncap = il->cap ? il->cap * 2 : 32;
                    ignore_rule_t *nb = (ignore_rule_t *)realloc(il->rules, ncap * sizeof(ignore_rule_t));
                    if (nb) { il->rules = nb; il->cap = ncap; }
                }
                if (il->count < il->cap) {
                    il->rules[il->count].pattern = _strdup(p);
                    il->rules[il->count].dir_only = dir_only;
                    il->rules[il->count].anchored = anchored;
                    il->count++;
                }
            }
        }
        if (!eol) break;
        p = eol + 1;
        while (*p == '\r' || *p == '\n') p++;
    }
    free(buf);
}

static bool is_ignored(const ignore_list_t *il, const char *rel_path, bool is_dir) {
    const char *base = strrchr(rel_path, '/');
    base = base ? base + 1 : rel_path;
    for (size_t i = 0; i < il->count; i++) {
        const ignore_rule_t *r = &il->rules[i];
        if (r->dir_only && !is_dir) continue;
        if (r->anchored || strchr(r->pattern, '/') != NULL) {
            if (glob_match(r->pattern, rel_path)) return true;
        } else {
            if (glob_match(r->pattern, base)) return true;
        }
    }
    return false;
}

static bool find_git_dirs(const wchar_t *start_dir, wchar_t *out_work_tree, size_t wt_cap,
                          wchar_t *out_git_dir, size_t gd_cap) {
    wchar_t cur[MAX_PATH * 4];
    wcsncpy(cur, start_dir, (sizeof(cur) / sizeof(cur[0])) - 1);
    cur[(sizeof(cur) / sizeof(cur[0])) - 1] = L'\0';

    for (;;) {
        wchar_t probe[MAX_PATH * 4];
        size_t clen = wcslen(cur);
        if (clen > 0 && cur[clen - 1] == L'\\') {
            swprintf(probe, sizeof(probe) / sizeof(probe[0]), L"%ls.git", cur);
        } else {
            swprintf(probe, sizeof(probe) / sizeof(probe[0]), L"%ls\\.git", cur);
        }

        DWORD attrs = GetFileAttributesW(probe);
        if (attrs != INVALID_FILE_ATTRIBUTES) {
            wcsncpy(out_work_tree, cur, wt_cap - 1);
            out_work_tree[wt_cap - 1] = L'\0';
            if (attrs & FILE_ATTRIBUTE_DIRECTORY) {
                wcsncpy(out_git_dir, probe, gd_cap - 1);
                out_git_dir[gd_cap - 1] = L'\0';
                return true;
            } else {
                /* Worktree / submodule .git file: "gitdir: <path>" */
                HANDLE f = CreateFileW(probe, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
                if (f != INVALID_HANDLE_VALUE) {
                    char buf[1024];
                    DWORD rd = 0;
                    if (ReadFile(f, buf, sizeof(buf) - 1, &rd, NULL) && rd > 8) {
                        buf[rd] = '\0';
                        char *nl = strpbrk(buf, "\r\n");
                        if (nl) *nl = '\0';
                        if (_strnicmp(buf, "gitdir:", 7) == 0) {
                            char *gd = buf + 7;
                            while (*gd == ' ' || *gd == '\t') gd++;
                            wchar_t wgd[MAX_PATH * 4];
                            MultiByteToWideChar(CP_UTF8, 0, gd, -1, wgd, (int)(sizeof(wgd) / sizeof(wgd[0])));
                            for (wchar_t *q = wgd; *q; q++) if (*q == L'/') *q = L'\\';
                            if (wgd[0] == L'\\' || (wgd[0] && wgd[1] == L':')) {
                                wcsncpy(out_git_dir, wgd, gd_cap - 1);
                            } else {
                                swprintf(out_git_dir, gd_cap, L"%ls\\%ls", cur, wgd);
                            }
                            out_git_dir[gd_cap - 1] = L'\0';
                            CloseHandle(f);
                            return true;
                        }
                    }
                    CloseHandle(f);
                }
            }
        }

        /* Move to parent directory */
        wchar_t *slash = wcsrchr(cur, L'\\');
        if (!slash) break;
        if (slash == cur || (slash == cur + 2 && cur[1] == L':')) {
            if (slash[1] == L'\0') break;
            slash[1] = L'\0';
        } else {
            *slash = L'\0';
        }
    }
    return false;
}

static void parse_head(const wchar_t *git_dir, git_status_t *out) {
    wchar_t head_path[MAX_PATH * 4];
    swprintf(head_path, sizeof(head_path) / sizeof(head_path[0]), L"%ls\\HEAD", git_dir);
    HANDLE f = CreateFileW(head_path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE) {
        wcscpy(out->branch, L"HEAD");
        return;
    }
    char buf[512];
    DWORD rd = 0;
    if (!ReadFile(f, buf, sizeof(buf) - 1, &rd, NULL) || rd == 0) {
        CloseHandle(f);
        wcscpy(out->branch, L"HEAD");
        return;
    }
    CloseHandle(f);
    buf[rd] = '\0';
    char *nl = strpbrk(buf, "\r\n");
    if (nl) *nl = '\0';

    const char *ref_prefix = "ref: refs/heads/";
    if (strncmp(buf, ref_prefix, strlen(ref_prefix)) == 0) {
        const char *b = buf + strlen(ref_prefix);
        MultiByteToWideChar(CP_UTF8, 0, b, -1, out->branch, (int)(sizeof(out->branch) / sizeof(out->branch[0])));
        out->is_detached = false;
    } else if (strncmp(buf, "ref: ", 5) == 0) {
        const char *b = buf + 5;
        MultiByteToWideChar(CP_UTF8, 0, b, -1, out->branch, (int)(sizeof(out->branch) / sizeof(out->branch[0])));
        out->is_detached = false;
    } else {
        out->is_detached = true;
        char short_hash[16];
        size_t n = strlen(buf);
        if (n > 7) n = 7;
        memcpy(short_hash, buf, n);
        short_hash[n] = '\0';
        MultiByteToWideChar(CP_UTF8, 0, short_hash, -1, out->branch, (int)(sizeof(out->branch) / sizeof(out->branch[0])));
    }
}

static bool parse_git_index(const wchar_t *git_dir, idx_table_t *table) {
    wchar_t idx_path[MAX_PATH * 4];
    swprintf(idx_path, sizeof(idx_path) / sizeof(idx_path[0]), L"%ls\\index", git_dir);
    HANDLE f = CreateFileW(idx_path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE) {
        return idx_table_init(table, 16); /* Empty repo with no index yet */
    }

    LARGE_INTEGER fsz;
    if (!GetFileSizeEx(f, &fsz) || fsz.QuadPart < 12 || fsz.QuadPart > (64LL * 1024 * 1024)) {
        CloseHandle(f);
        return idx_table_init(table, 16);
    }

    size_t len = (size_t)fsz.QuadPart;
    uint8_t *data = (uint8_t *)malloc(len);
    if (!data) { CloseHandle(f); return false; }
    DWORD rd = 0;
    BOOL ok = ReadFile(f, data, (DWORD)len, &rd, NULL);
    CloseHandle(f);
    if (!ok || rd < 12 || memcmp(data, "DIRC", 4) != 0) {
        free(data);
        return idx_table_init(table, 16);
    }

    uint32_t version = read_be32(data + 4);
    uint32_t count = read_be32(data + 8);
    if (version < 2 || version > 4) {
        free(data);
        return idx_table_init(table, 16);
    }
    if (!idx_table_init(table, count > 0 ? count : 16)) {
        free(data);
        return false;
    }

    size_t off = 12;
    char prev_path[4096];
    prev_path[0] = '\0';

    for (uint32_t i = 0; i < count && off + 62 <= len; i++) {
        size_t entry_start = off;
        uint32_t mtime_s = read_be32(data + off + 8);
        uint32_t file_size = read_be32(data + off + 36);
        uint16_t flags = read_be16(data + off + 60);
        off += 62;

        if (version >= 3 && (flags & 0x4000)) {
            if (off + 2 > len) break;
            off += 2; /* extended flags */
        }

        char cur_path[4096];
        if (version == 4) {
            /* Prefix-compressed varint */
            size_t strip_len = 0;
            uint8_t c;
            do {
                if (off >= len) break;
                c = data[off++];
                strip_len = (strip_len << 7) | (c & 0x7F);
                if (c & 0x80) strip_len++;
            } while (c & 0x80);
            size_t plen = strlen(prev_path);
            size_t keep = (strip_len <= plen) ? (plen - strip_len) : 0;
            const char *suf = (const char *)(data + off);
            size_t slen = strnlen(suf, len - off);
            if (keep + slen < sizeof(cur_path)) {
                memcpy(cur_path, prev_path, keep);
                memcpy(cur_path + keep, suf, slen);
                cur_path[keep + slen] = '\0';
            } else {
                cur_path[0] = '\0';
            }
            off += slen + 1;
            strcpy(prev_path, cur_path);
        } else {
            const char *name = (const char *)(data + off);
            size_t nlen = strnlen(name, len - off);
            if (nlen < sizeof(cur_path)) {
                memcpy(cur_path, name, nlen);
                cur_path[nlen] = '\0';
            } else {
                cur_path[0] = '\0';
            }
            off += nlen + 1;
            /* Pad to 8-byte boundary from entry_start */
            size_t consumed = off - entry_start;
            size_t rem = consumed & 7u;
            if (rem != 0) off += (8u - rem);
        }

        if (cur_path[0]) {
            idx_table_insert(table, cur_path, mtime_s, file_size);
        }
    }

    free(data);
    return true;
}

static uint32_t filetime_to_unix_sec(FILETIME ft) {
    ULARGE_INTEGER ull;
    ull.LowPart = ft.dwLowDateTime;
    ull.HighPart = ft.dwHighDateTime;
    if (ull.QuadPart < 116444736000000000ULL) return 0;
    return (uint32_t)((ull.QuadPart - 116444736000000000ULL) / 10000000ULL);
}

static void check_tracked_modified(const wchar_t *work_tree, const idx_table_t *table, git_status_t *out) {
    for (size_t i = 0; i < table->cap; i++) {
        if (!table->slots[i].path) continue;
        wchar_t rel_w[MAX_PATH * 2];
        MultiByteToWideChar(CP_UTF8, 0, table->slots[i].path, -1, rel_w, (int)(sizeof(rel_w) / sizeof(rel_w[0])));
        for (wchar_t *p = rel_w; *p; p++) if (*p == L'/') *p = L'\\';

        wchar_t full[MAX_PATH * 4];
        size_t wlen = wcslen(work_tree);
        if (wlen > 0 && work_tree[wlen - 1] == L'\\') {
            swprintf(full, sizeof(full) / sizeof(full[0]), L"%ls%ls", work_tree, rel_w);
        } else {
            swprintf(full, sizeof(full) / sizeof(full[0]), L"%ls\\%ls", work_tree, rel_w);
        }

        WIN32_FILE_ATTRIBUTE_DATA fad;
        if (!GetFileAttributesExW(full, GetFileExInfoStandard, &fad) ||
            (fad.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
            out->is_dirty = true;
            return;
        }
        if (fad.nFileSizeHigh != 0 || fad.nFileSizeLow != table->slots[i].size) {
            out->is_dirty = true;
            return;
        }
        if (table->slots[i].mtime_s != 0) {
            uint32_t disk_mtime = filetime_to_unix_sec(fad.ftLastWriteTime);
            uint32_t diff = (disk_mtime > table->slots[i].mtime_s)
                            ? (disk_mtime - table->slots[i].mtime_s)
                            : (table->slots[i].mtime_s - disk_mtime);
            if (diff > 1) {
                out->is_dirty = true;
                return;
            }
        }
    }
}

static void scan_untracked_walk(const wchar_t *work_tree, const char *rel_prefix,
                                const idx_table_t *table, const ignore_list_t *il,
                                git_status_t *out, size_t *visited) {
    if (out->has_untracked || *visited > 4096) return;

    wchar_t wdir[MAX_PATH * 4];
    if (rel_prefix[0]) {
        wchar_t wrel[MAX_PATH * 2];
        MultiByteToWideChar(CP_UTF8, 0, rel_prefix, -1, wrel, (int)(sizeof(wrel) / sizeof(wrel[0])));
        for (wchar_t *q = wrel; *q; q++) if (*q == L'/') *q = L'\\';
        swprintf(wdir, sizeof(wdir) / sizeof(wdir[0]), L"%ls\\%ls\\*", work_tree, wrel);
    } else {
        size_t wlen = wcslen(work_tree);
        if (wlen > 0 && work_tree[wlen - 1] == L'\\') {
            swprintf(wdir, sizeof(wdir) / sizeof(wdir[0]), L"%ls*", work_tree);
        } else {
            swprintf(wdir, sizeof(wdir) / sizeof(wdir[0]), L"%ls\\*", work_tree);
        }
    }

    WIN32_FIND_DATAW fd;
    HANDLE hf = FindFirstFileW(wdir, &fd);
    if (hf == INVALID_HANDLE_VALUE) return;

    do {
        if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0 ||
            _wcsicmp(fd.cFileName, L".git") == 0) {
            continue;
        }
        (*visited)++;
        if (*visited > 4096) break;

        char name_utf8[MAX_PATH * 3];
        WideCharToMultiByte(CP_UTF8, 0, fd.cFileName, -1, name_utf8, sizeof(name_utf8), NULL, NULL);

        char rel[4096];
        if (rel_prefix[0]) {
            snprintf(rel, sizeof(rel), "%s/%s", rel_prefix, name_utf8);
        } else {
            snprintf(rel, sizeof(rel), "%s", name_utf8);
        }

        bool is_dir = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        if (is_ignored(il, rel, is_dir)) continue;

        if (is_dir) {
            scan_untracked_walk(work_tree, rel, table, il, out, visited);
            if (out->has_untracked) break;
        } else {
            if (!idx_table_contains(table, rel)) {
                out->has_untracked = true;
                out->is_dirty = true;
                break;
            }
        }
    } while (FindNextFileW(hf, &fd));

    FindClose(hf);
}

bool git_native_inspect(const wchar_t *cwd, git_status_t *out) {
    if (!out) return false;
    memset(out, 0, sizeof(*out));
    if (!cwd || !*cwd) return false;

    wchar_t work_tree[MAX_PATH * 4];
    wchar_t git_dir[MAX_PATH * 4];
    if (!find_git_dirs(cwd, work_tree, sizeof(work_tree) / sizeof(work_tree[0]),
                       git_dir, sizeof(git_dir) / sizeof(git_dir[0]))) {
        return false;
    }

    out->in_repo = true;
    parse_head(git_dir, out);

    idx_table_t table = { 0 };
    if (parse_git_index(git_dir, &table)) {
        check_tracked_modified(work_tree, &table, out);

        ignore_list_t il = { 0 };
        wchar_t ig1[MAX_PATH * 4], ig2[MAX_PATH * 4];
        swprintf(ig1, sizeof(ig1) / sizeof(ig1[0]), L"%ls\\.gitignore", work_tree);
        swprintf(ig2, sizeof(ig2) / sizeof(ig2[0]), L"%ls\\info\\exclude", git_dir);
        load_ignore_file(&il, ig1);
        load_ignore_file(&il, ig2);

        size_t visited = 0;
        scan_untracked_walk(work_tree, "", &table, &il, out, &visited);

        ignore_list_free(&il);
        idx_table_free(&table);
    }

    return true;
}
