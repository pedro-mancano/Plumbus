#include "cmdline.h"
#include <windows.h>
#include <stdlib.h>
#include <string.h>
#include <wctype.h>

static wchar_t *dup_wstr_range(const wchar_t *s, size_t len) {
    wchar_t *p = (wchar_t *)malloc((len + 1) * sizeof(wchar_t));
    if (!p) return NULL;
    if (len > 0) memcpy(p, s, len * sizeof(wchar_t));
    p[len] = L'\0';
    return p;
}

static bool is_space_w(wchar_t c) {
    return c == L' ' || c == L'\t';
}

static const wchar_t *skip_exe_token(const wchar_t *p) {
    while (is_space_w(*p)) p++;
    if (*p == L'"') {
        p++;
        while (*p && *p != L'"') p++;
        if (*p == L'"') p++;
    } else {
        while (*p && !is_space_w(*p)) p++;
    }
    return p;
}

static bool has_cmd_special_chars(const wchar_t *s, size_t len) {
    for (size_t i = 0; i < len; i++) {
        switch (s[i]) {
            case L'&':
            case L'<':
            case L'>':
            case L'(':
            case L')':
            case L'@':
            case L'^':
            case L'|':
                return true;
            default:
                break;
        }
    }
    return false;
}

static bool file_exists_exact_or_pathext(const wchar_t *candidate) {
    DWORD attrs = GetFileAttributesW(candidate);
    if (attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_DIRECTORY)) {
        return true;
    }

    /* Check with PATHEXT if no extension or direct miss */
    wchar_t pathext[512];
    DWORD n = GetEnvironmentVariableW(L"PATHEXT", pathext, (DWORD)(sizeof(pathext) / sizeof(pathext[0])));
    if (n == 0 || n >= sizeof(pathext) / sizeof(pathext[0])) {
        wcscpy(pathext, L".COM;.EXE;.BAT;.CMD");
    }

    size_t clen = wcslen(candidate);
    wchar_t *ctx = NULL;
    for (wchar_t *ext = wcstok_s(pathext, L";", &ctx); ext != NULL; ext = wcstok_s(NULL, L";", &ctx)) {
        size_t elen = wcslen(ext);
        wchar_t *probe = (wchar_t *)malloc((clen + elen + 1) * sizeof(wchar_t));
        if (!probe) continue;
        memcpy(probe, candidate, clen * sizeof(wchar_t));
        memcpy(probe + clen, ext, elen * sizeof(wchar_t));
        probe[clen + elen] = L'\0';
        attrs = GetFileAttributesW(probe);
        free(probe);
        if (attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_DIRECTORY)) {
            return true;
        }
    }

    /* Also try SearchPathW for commands in PATH with spaces */
    if (SearchPathW(NULL, candidate, L".exe", 0, NULL, NULL) > 0) return true;
    if (SearchPathW(NULL, candidate, L".bat", 0, NULL, NULL) > 0) return true;
    if (SearchPathW(NULL, candidate, L".cmd", 0, NULL, NULL) > 0) return true;
    if (SearchPathW(NULL, candidate, L".com", 0, NULL, NULL) > 0) return true;
    return false;
}

wchar_t *cmdline_strip_cmd_quotes(const wchar_t *cmd_str, bool force_strip_s) {
    if (!cmd_str) return dup_wstr_range(L"", 0);

    const wchar_t *p = cmd_str;
    while (is_space_w(*p)) p++;

    size_t len = wcslen(p);
    while (len > 0 && is_space_w(p[len - 1])) {
        len--;
    }
    if (len == 0) return dup_wstr_range(L"", 0);

    if (!force_strip_s) {
        size_t quote_count = 0;
        for (size_t i = 0; i < len; i++) {
            if (p[i] == L'"') quote_count++;
        }

        if (quote_count == 2 && p[0] == L'"' && p[len - 1] == L'"' && len >= 2) {
            const wchar_t *inner = p + 1;
            size_t inner_len = len - 2;
            bool has_space = false;
            for (size_t i = 0; i < inner_len; i++) {
                if (is_space_w(inner[i])) {
                    has_space = true;
                    break;
                }
            }
            if (has_space && !has_cmd_special_chars(inner, inner_len)) {
                wchar_t *candidate = dup_wstr_range(inner, inner_len);
                if (candidate) {
                    bool is_exe = file_exists_exact_or_pathext(candidate);
                    free(candidate);
                    if (is_exe) {
                        return dup_wstr_range(p, len);
                    }
                }
            }
        }
    }

    /* Strip first '"' (if at start or anywhere? Phase 0: cmd strips first and last quote) */
    const wchar_t *first_q = NULL;
    const wchar_t *last_q = NULL;
    for (size_t i = 0; i < len; i++) {
        if (p[i] == L'"') {
            if (!first_q) first_q = p + i;
            last_q = p + i;
        }
    }

    if (p[0] == L'"' && last_q && last_q != p) {
        wchar_t *out = (wchar_t *)malloc((len + 1) * sizeof(wchar_t));
        if (!out) return NULL;
        size_t oi = 0;
        for (size_t i = 0; i < len; i++) {
            const wchar_t *cur = p + i;
            if (cur == p || cur == last_q) continue;
            out[oi++] = *cur;
        }
        out[oi] = L'\0';
        return out;
    }

    return dup_wstr_range(p, len);
}

static bool match_ci_prefix(const wchar_t *p, const wchar_t *lit) {
    while (*lit) {
        if (towlower(*p) != towlower(*lit)) return false;
        p++;
        lit++;
    }
    return true;
}

bool cmdline_parse(const wchar_t *raw_cmdline, cmd_options_t *out_opts) {
    if (!out_opts) return false;
    memset(out_opts, 0, sizeof(*out_opts));
    out_opts->mode = CMD_MODE_INTERACTIVE;
    out_opts->extensions = true;

    if (!raw_cmdline) return true;

    const wchar_t *p = skip_exe_token(raw_cmdline);
    out_opts->raw_after_exe = p;

    while (*p) {
        while (is_space_w(*p)) p++;
        if (!*p) break;

        if (match_ci_prefix(p, L"--__bench-init") && (p[14] == L'\0' || is_space_w(p[14]))) {
            out_opts->mode = CMD_MODE_BENCH_INIT;
            return true;
        }

        if (match_ci_prefix(p, L"--__sync-state") && is_space_w(p[14])) {
            p += 14;
            while (is_space_w(*p)) p++;
            const wchar_t *path_start = p;
            size_t path_len = 0;
            if (*p == L'"') {
                p++;
                path_start = p;
                while (*p && *p != L'"') p++;
                path_len = (size_t)(p - path_start);
                if (*p == L'"') p++;
            } else {
                while (*p && !is_space_w(*p)) p++;
                path_len = (size_t)(p - path_start);
            }
            out_opts->sync_file_path = dup_wstr_range(path_start, path_len);
            while (is_space_w(*p)) p++;
            out_opts->sync_exit_code = _wtoi(p);
            out_opts->mode = CMD_MODE_INTERNAL_SYNC;
            return true;
        }

        if (*p != L'/') {
            /* Unknown non-switch token before /c or /k: stop switch parsing */
            break;
        }

        p++; /* skip '/' */
        if (!*p) break;

        wchar_t sw = towlower(*p);
        p++;

        if (sw == L'c' || sw == L'k') {
            out_opts->mode = (sw == L'c') ? CMD_MODE_EXEC_EXIT : CMD_MODE_EXEC_KEEP;
            out_opts->command_raw = p;
            out_opts->command_parsed = cmdline_strip_cmd_quotes(p, out_opts->strip_quotes);
            return true;
        } else if (sw == L'?') {
            out_opts->mode = CMD_MODE_HELP;
            return true;
        } else if (sw == L's') {
            out_opts->strip_quotes = true;
        } else if (sw == L'q') {
            out_opts->echo_off = true;
        } else if (sw == L'd') {
            out_opts->disable_autorun = true;
        } else if (sw == L'a') {
            out_opts->output_unicode = false;
        } else if (sw == L'u') {
            out_opts->output_unicode = true;
        } else if (sw == L'x') {
            out_opts->extensions_set = true;
            out_opts->extensions = true;
        } else if (sw == L'y') {
            out_opts->extensions_set = true;
            out_opts->extensions = false;
        } else if (sw == L'v' && *p == L':') {
            p++;
            if (match_ci_prefix(p, L"on")) {
                out_opts->delayed_expansion_set = true;
                out_opts->delayed_expansion = true;
                p += 2;
            } else if (match_ci_prefix(p, L"off")) {
                out_opts->delayed_expansion_set = true;
                out_opts->delayed_expansion = false;
                p += 3;
            }
        } else if (sw == L'e' && *p == L':') {
            p++;
            if (match_ci_prefix(p, L"on")) {
                out_opts->extensions_set = true;
                out_opts->extensions = true;
                p += 2;
            } else if (match_ci_prefix(p, L"off")) {
                out_opts->extensions_set = true;
                out_opts->extensions = false;
                p += 3;
            }
        } else if (sw == L'f' && *p == L':') {
            p++;
            if (match_ci_prefix(p, L"on")) {
                out_opts->completion_set = true;
                out_opts->completion_enabled = true;
                p += 2;
            } else if (match_ci_prefix(p, L"off")) {
                out_opts->completion_set = true;
                out_opts->completion_enabled = false;
                p += 3;
            }
        } else if (sw == L't' && *p == L':') {
            p++;
            size_t ci = 0;
            while (*p && !is_space_w(*p) && *p != L'/' && ci < 3) {
                out_opts->color_attr[ci++] = *p++;
            }
            out_opts->color_attr[ci] = L'\0';
        }
        /* Glued flags like /d/c"echo hi" continue the loop immediately */
    }

    return true;
}

void cmdline_free(cmd_options_t *opts) {
    if (!opts) return;
    free(opts->command_parsed);
    free(opts->sync_file_path);
    opts->command_parsed = NULL;
    opts->sync_file_path = NULL;
}
