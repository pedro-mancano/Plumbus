#include "resolver.h"
#include <windows.h>
#include <stdlib.h>
#include <string.h>
#include <wctype.h>

static const wchar_t *const g_cmd_internals[] = {
    L"assoc", L"break", L"call", L"cd", L"chdir", L"cls", L"color", L"copy",
    L"date", L"del", L"dir", L"echo", L"endlocal", L"erase", L"exit", L"for",
    L"ftype", L"goto", L"if", L"keys", L"md", L"mkdir", L"mklink", L"move",
    L"path", L"pause", L"popd", L"prompt", L"pushd", L"rd", L"rem", L"ren",
    L"rename", L"rmdir", L"set", L"setlocal", L"shift", L"start", L"time",
    L"title", L"type", L"ver", L"verify", L"vol"
};

bool resolver_is_cmd_internal(const wchar_t *name) {
    if (!name || !*name) return false;
    size_t count = sizeof(g_cmd_internals) / sizeof(g_cmd_internals[0]);
    for (size_t i = 0; i < count; i++) {
        if (_wcsicmp(name, g_cmd_internals[i]) == 0) return true;
    }
    return false;
}

static bool has_shell_metachar_outside_quotes(const wchar_t *line) {
    bool in_quotes = false;
    for (const wchar_t *p = line; *p; p++) {
        if (*p == L'"') {
            in_quotes = !in_quotes;
            continue;
        }
        if (*p == L'^' && p[1] != L'\0') {
            return true;
        }
        if (*p == L'%') {
            return true;
        }
        if (!in_quotes) {
            switch (*p) {
                case L'|':
                case L'&':
                case L'<':
                case L'>':
                case L'(':
                case L')':
                    return true;
                default:
                    break;
            }
        }
    }
    return false;
}

static void extract_first_token(const wchar_t *line, wchar_t *tok, size_t tok_cap) {
    tok[0] = L'\0';
    if (!line) return;
    const wchar_t *p = line;
    while (*p == L' ' || *p == L'\t') p++;
    if (!*p) return;

    /* Handle leading '@' (silent prefix) */
    if (*p == L'@') {
        p++;
        while (*p == L' ' || *p == L'\t') p++;
    }

    size_t out = 0;
    if (*p == L'"') {
        p++;
        while (*p && *p != L'"' && out + 1 < tok_cap) {
            tok[out++] = *p++;
        }
    } else {
        while (*p && *p != L' ' && *p != L'\t' &&
               *p != L'|' && *p != L'&' && *p != L'<' && *p != L'>' &&
               *p != L'(' && *p != L')' && out + 1 < tok_cap) {
            tok[out++] = *p++;
        }
    }
    tok[out] = L'\0';
}

static bool is_file_w(const wchar_t *path) {
    DWORD a = GetFileAttributesW(path);
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

static bool ends_with_ci(const wchar_t *s, const wchar_t *suffix) {
    size_t slen = wcslen(s);
    size_t xlen = wcslen(suffix);
    if (slen < xlen) return false;
    return _wcsicmp(s + slen - xlen, suffix) == 0;
}

static bool has_path_sep(const wchar_t *s) {
    return wcschr(s, L'\\') != NULL || wcschr(s, L'/') != NULL || wcschr(s, L':') != NULL;
}

static bool has_extension(const wchar_t *s) {
    const wchar_t *slash = wcsrchr(s, L'\\');
    const wchar_t *fslash = wcsrchr(s, L'/');
    if (fslash && (!slash || fslash > slash)) slash = fslash;
    const wchar_t *base = slash ? slash + 1 : s;
    return wcschr(base, L'.') != NULL;
}

static bool probe_dir_with_pathext(const wchar_t *dir, const wchar_t *name, const wchar_t *pathext_list, wchar_t *out_path, size_t out_cap) {
    wchar_t base[MAX_PATH * 4];
    if (dir && *dir) {
        size_t dlen = wcslen(dir);
        if (dlen + wcslen(name) + 32 >= sizeof(base) / sizeof(base[0])) return false;
        wcscpy(base, dir);
        if (base[dlen - 1] != L'\\' && base[dlen - 1] != L'/') {
            wcscat(base, L"\\");
        }
        wcscat(base, name);
    } else {
        if (wcslen(name) + 32 >= sizeof(base) / sizeof(base[0])) return false;
        wcscpy(base, name);
    }

    if (has_extension(name)) {
        if (is_file_w(base)) {
            DWORD n = GetFullPathNameW(base, (DWORD)out_cap, out_path, NULL);
            if (n == 0 || n >= out_cap) wcsncpy(out_path, base, out_cap - 1);
            out_path[out_cap - 1] = L'\0';
            return true;
        }
        return false;
    }

    wchar_t exts[512];
    wcsncpy(exts, pathext_list, sizeof(exts) / sizeof(exts[0]) - 1);
    exts[sizeof(exts) / sizeof(exts[0]) - 1] = L'\0';

    wchar_t *ctx = NULL;
    for (wchar_t *ext = wcstok_s(exts, L";", &ctx); ext != NULL; ext = wcstok_s(NULL, L";", &ctx)) {
        wchar_t cand[MAX_PATH * 4];
        if (wcslen(base) + wcslen(ext) + 1 >= sizeof(cand) / sizeof(cand[0])) continue;
        wcscpy(cand, base);
        wcscat(cand, ext);
        if (is_file_w(cand)) {
            DWORD n = GetFullPathNameW(cand, (DWORD)out_cap, out_path, NULL);
            if (n == 0 || n >= out_cap) wcsncpy(out_path, cand, out_cap - 1);
            out_path[out_cap - 1] = L'\0';
            return true;
        }
    }
    return false;
}

static bool resolve_executable_precedence(const wchar_t *token, wchar_t *out_path, size_t out_cap) {
    wchar_t pathext[512];
    DWORD n = GetEnvironmentVariableW(L"PATHEXT", pathext, (DWORD)(sizeof(pathext) / sizeof(pathext[0])));
    if (n == 0 || n >= sizeof(pathext) / sizeof(pathext[0])) {
        wcscpy(pathext, L".COM;.EXE;.BAT;.CMD;.VBS;.VBE;.JS;.JSE;.WSF;.WSH;.MSC");
    }

    if (has_path_sep(token)) {
        return probe_dir_with_pathext(NULL, token, pathext, out_path, out_cap);
    }

    /* Check NoDefaultCurrentDirectoryInExePath */
    wchar_t nodir[16];
    bool skip_cwd = (GetEnvironmentVariableW(L"NoDefaultCurrentDirectoryInExePath", nodir, 16) > 0);

    if (!skip_cwd) {
        wchar_t cwd[MAX_PATH * 4];
        DWORD clen = GetCurrentDirectoryW((DWORD)(sizeof(cwd) / sizeof(cwd[0])), cwd);
        if (clen > 0 && clen < sizeof(cwd) / sizeof(cwd[0])) {
            if (probe_dir_with_pathext(cwd, token, pathext, out_path, out_cap)) {
                return true;
            }
        }
    }

    DWORD path_need = GetEnvironmentVariableW(L"PATH", NULL, 0);
    if (path_need > 0) {
        wchar_t *path_buf = (wchar_t *)malloc((path_need + 1) * sizeof(wchar_t));
        if (path_buf) {
            if (GetEnvironmentVariableW(L"PATH", path_buf, path_need + 1) > 0) {
                wchar_t *ctx = NULL;
                for (wchar_t *dir = wcstok_s(path_buf, L";", &ctx); dir != NULL; dir = wcstok_s(NULL, L";", &ctx)) {
                    if (!*dir) continue;
                    /* Strip surrounding quotes if any in PATH */
                    size_t dlen = wcslen(dir);
                    if (dlen >= 2 && dir[0] == L'"' && dir[dlen - 1] == L'"') {
                        dir[dlen - 1] = L'\0';
                        dir++;
                    }
                    if (probe_dir_with_pathext(dir, token, pathext, out_path, out_cap)) {
                        free(path_buf);
                        return true;
                    }
                }
            }
            free(path_buf);
        }
    }

    return false;
}

bool resolver_command_exists(const wchar_t *token) {
    if (!token || !*token) return false;
    if (wcslen(token) == 2 && iswalpha(token[0]) && token[1] == L':') return true;
    if (resolver_is_cmd_internal(token)) return true;
    wchar_t dummy[MAX_PATH * 4];
    return resolve_executable_precedence(token, dummy, sizeof(dummy) / sizeof(dummy[0]));
}

static bool is_native_fastpath_token(const wchar_t *tok) {
    if (wcslen(tok) == 2 && iswalpha(tok[0]) && tok[1] == L':') return true;
    if (_wcsicmp(tok, L"cd") == 0 || _wcsicmp(tok, L"chdir") == 0 ||
        _wcsicmp(tok, L"pushd") == 0 || _wcsicmp(tok, L"popd") == 0 ||
        _wcsicmp(tok, L"exit") == 0) {
        return true;
    }
    return false;
}

bool resolver_classify_and_find(const wchar_t *line, cmd_resolution_t *out_res) {
    if (!out_res) return false;
    out_res->kind = CMD_KIND_EMPTY;
    out_res->resolved_path[0] = L'\0';
    out_res->has_shell_meta = false;

    if (!line) return true;

    const wchar_t *p = line;
    while (*p == L' ' || *p == L'\t') p++;
    if (!*p) return true;

    out_res->has_shell_meta = has_shell_metachar_outside_quotes(p);
    if (out_res->has_shell_meta) {
        out_res->kind = CMD_KIND_CMD_COMPOUND;
        return true;
    }

    wchar_t tok[MAX_PATH * 4];
    extract_first_token(p, tok, sizeof(tok) / sizeof(tok[0]));
    if (!tok[0]) return true;

    if (is_native_fastpath_token(tok)) {
        out_res->kind = CMD_KIND_NATIVE_FASTPATH;
        return true;
    }

    /* Precedence 1: CMD internal commands */
    if (resolver_is_cmd_internal(tok)) {
        out_res->kind = CMD_KIND_CMD_INTERNAL;
        return true;
    }

    /* Precedence 2 & 3: CWD then PATH x PATHEXT */
    if (resolve_executable_precedence(tok, out_res->resolved_path,
                                      sizeof(out_res->resolved_path) / sizeof(out_res->resolved_path[0]))) {
        if (ends_with_ci(out_res->resolved_path, L".bat") ||
            ends_with_ci(out_res->resolved_path, L".cmd")) {
            out_res->kind = CMD_KIND_BATCH_SCRIPT;
        } else if (ends_with_ci(out_res->resolved_path, L".exe") ||
                   ends_with_ci(out_res->resolved_path, L".com")) {
            out_res->kind = CMD_KIND_EXTERNAL_EXE;
        } else {
            /* .vbs, .js, .msc, etc. -> delegate to cmd.exe */
            out_res->kind = CMD_KIND_CMD_COMPOUND;
        }
        return true;
    }

    /* Unknown command -> let cmd.exe handle error / PATHEXT associations */
    out_res->kind = CMD_KIND_CMD_COMPOUND;
    return true;
}
