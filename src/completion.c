#include "completion.h"
#include <windows.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

static const wchar_t *const g_git_subcommands[] = {
    L"add", L"bisect", L"branch", L"checkout", L"cherry-pick", L"clean",
    L"clone", L"commit", L"config", L"diff", L"fetch", L"grep", L"init",
    L"log", L"merge", L"mv", L"pull", L"push", L"rebase", L"reflog",
    L"remote", L"reset", L"restore", L"revert", L"rm", L"show", L"stash",
    L"status", L"switch", L"tag", L"worktree"
};

static bool add_candidate(completion_list_t *out, size_t *cap, const wchar_t *val) {
    if (out->count == *cap) {
        size_t ncap = (*cap) ? (*cap) * 2 : 32;
        wchar_t **nb = (wchar_t **)realloc(out->items, ncap * sizeof(wchar_t *));
        if (!nb) return false;
        out->items = nb;
        *cap = ncap;
    }
    wchar_t *copy = _wcsdup(val);
    if (!copy) return false;
    out->items[out->count++] = copy;
    return true;
}

static int cmp_wstr_ci(const void *a, const void *b) {
    const wchar_t *sa = *(const wchar_t *const *)a;
    const wchar_t *sb = *(const wchar_t *const *)b;
    return _wcsicmp(sa, sb);
}

static bool is_first_token_git(const wchar_t *line, size_t tok_start) {
    const wchar_t *p = line;
    while (*p == L' ' || *p == L'\t') p++;
    if ((size_t)(p - line) >= tok_start) return false;
    if (_wcsnicmp(p, L"git", 3) != 0) return false;
    if (p[3] != L' ' && p[3] != L'\t') return false;
    p += 3;
    while (*p == L' ' || *p == L'\t') p++;
    return (size_t)(p - line) == tok_start;
}

void completion_generate(const wchar_t *line, size_t cursor, completion_list_t *out) {
    if (!out) return;
    memset(out, 0, sizeof(*out));
    if (!line) return;

    size_t len = wcslen(line);
    if (cursor > len) cursor = len;

    /* Find start of current token (respecting quotes) */
    size_t start = cursor;
    bool in_quote = false;
    for (size_t i = 0; i < cursor; i++) {
        if (line[i] == L'"') in_quote = !in_quote;
    }
    if (in_quote) {
        while (start > 0 && line[start - 1] != L'"') start--;
        if (start > 0 && line[start - 1] == L'"') start--;
    } else {
        while (start > 0 && line[start - 1] != L' ' && line[start - 1] != L'\t') start--;
    }

    out->replace_start = start;
    out->replace_len = cursor - start;

    wchar_t prefix[MAX_PATH * 4];
    size_t plen = out->replace_len;
    if (plen >= sizeof(prefix) / sizeof(prefix[0])) plen = (sizeof(prefix) / sizeof(prefix[0])) - 1;
    memcpy(prefix, line + start, plen * sizeof(wchar_t));
    prefix[plen] = L'\0';

    bool had_leading_quote = (prefix[0] == L'"');
    const wchar_t *clean_prefix = had_leading_quote ? (prefix + 1) : prefix;
    size_t cap = 0;

    /* 1. %VAR% environment variable completion */
    if (!had_leading_quote && clean_prefix[0] == L'%' && wcschr(clean_prefix + 1, L'%') == NULL) {
        const wchar_t *var_pref = clean_prefix + 1;
        size_t vplen = wcslen(var_pref);
        LPWCH env = GetEnvironmentStringsW();
        if (env) {
            for (const wchar_t *p = env; *p; p += wcslen(p) + 1) {
                if (p[0] == L'=') continue;
                const wchar_t *eq = wcschr(p, L'=');
                if (!eq) continue;
                size_t nlen = (size_t)(eq - p);
                if (nlen >= vplen && _wcsnicmp(p, var_pref, vplen) == 0) {
                    wchar_t cand[512];
                    if (nlen + 3 < sizeof(cand) / sizeof(cand[0])) {
                        cand[0] = L'%';
                        memcpy(cand + 1, p, nlen * sizeof(wchar_t));
                        cand[1 + nlen] = L'%';
                        cand[2 + nlen] = L'\0';
                        add_candidate(out, &cap, cand);
                    }
                }
            }
            FreeEnvironmentStringsW(env);
        }
        if (out->count > 1) qsort(out->items, out->count, sizeof(wchar_t *), cmp_wstr_ci);
        return;
    }

    /* 2. Git subcommands when completing second token after "git" */
    if (!had_leading_quote && wcschr(clean_prefix, L'\\') == NULL && wcschr(clean_prefix, L'/') == NULL &&
        is_first_token_git(line, start)) {
        size_t gcount = sizeof(g_git_subcommands) / sizeof(g_git_subcommands[0]);
        size_t cplen = wcslen(clean_prefix);
        for (size_t i = 0; i < gcount; i++) {
            if (_wcsnicmp(g_git_subcommands[i], clean_prefix, cplen) == 0) {
                add_candidate(out, &cap, g_git_subcommands[i]);
            }
        }
        if (out->count > 0) return;
    }

    /* 3. File / directory completion via FindFirstFileW */
    const wchar_t *last_sep = wcsrchr(clean_prefix, L'\\');
    const wchar_t *last_fsep = wcsrchr(clean_prefix, L'/');
    if (last_fsep && (!last_sep || last_fsep > last_sep)) last_sep = last_fsep;

    wchar_t dir_part[MAX_PATH * 4];
    const wchar_t *file_pref = clean_prefix;
    if (last_sep) {
        size_t dlen = (size_t)(last_sep - clean_prefix) + 1;
        memcpy(dir_part, clean_prefix, dlen * sizeof(wchar_t));
        dir_part[dlen] = L'\0';
        file_pref = last_sep + 1;
    } else {
        dir_part[0] = L'\0';
    }

    wchar_t pattern[MAX_PATH * 4];
    swprintf(pattern, sizeof(pattern) / sizeof(pattern[0]), L"%ls%ls*", dir_part, file_pref);

    WIN32_FIND_DATAW fd;
    HANDLE hf = FindFirstFileW(pattern, &fd);
    if (hf != INVALID_HANDLE_VALUE) {
        do {
            if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) continue;
            bool is_dir = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
            wchar_t combined[MAX_PATH * 4];
            swprintf(combined, sizeof(combined) / sizeof(combined[0]), L"%ls%ls%ls",
                     dir_part, fd.cFileName, is_dir ? L"\\" : L"");

            if (wcschr(combined, L' ') != NULL) {
                wchar_t quoted[MAX_PATH * 4 + 4];
                swprintf(quoted, sizeof(quoted) / sizeof(quoted[0]), L"\"%ls\"", combined);
                add_candidate(out, &cap, quoted);
            } else {
                add_candidate(out, &cap, combined);
            }
        } while (FindNextFileW(hf, &fd) && out->count < 256);
        FindClose(hf);
    }

    if (out->count > 1) {
        qsort(out->items, out->count, sizeof(wchar_t *), cmp_wstr_ci);
    }
}

void completion_free(completion_list_t *list) {
    if (!list) return;
    for (size_t i = 0; i < list->count; i++) {
        free(list->items[i]);
    }
    free(list->items);
    list->items = NULL;
    list->count = 0;
}
