#include "cmd_expand.h"
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wctype.h>

typedef struct {
    wchar_t *buf;
    size_t len;
    size_t cap;
} wbuf_t;

static bool wbuf_ensure(wbuf_t *b, size_t extra) {
    if (b->len + extra + 1 <= b->cap) return true;
    size_t ncap = b->cap ? b->cap * 2 : 256;
    while (ncap < b->len + extra + 1) ncap *= 2;
    wchar_t *nb = (wchar_t *)realloc(b->buf, ncap * sizeof(wchar_t));
    if (!nb) return false;
    b->buf = nb;
    b->cap = ncap;
    return true;
}

static void wbuf_append_n(wbuf_t *b, const wchar_t *s, size_t n) {
    if (n == 0) return;
    if (!wbuf_ensure(b, n)) return;
    memcpy(b->buf + b->len, s, n * sizeof(wchar_t));
    b->len += n;
    b->buf[b->len] = L'\0';
}

static void wbuf_append(wbuf_t *b, const wchar_t *s) {
    if (s) wbuf_append_n(b, s, wcslen(s));
}

static void wbuf_append_ch(wbuf_t *b, wchar_t c) {
    wbuf_append_n(b, &c, 1);
}

static wchar_t *wbuf_finish(wbuf_t *b) {
    if (!b->buf) {
        wchar_t *empty = (wchar_t *)malloc(sizeof(wchar_t));
        if (empty) empty[0] = L'\0';
        return empty;
    }
    b->buf[b->len] = L'\0';
    return b->buf;
}

bool cmd_lookup_var(const shell_ctx_t *ctx, const wchar_t *name, wchar_t **out_val) {
    if (!name || !*name || !out_val) return false;
    *out_val = NULL;

    DWORD needed = GetEnvironmentVariableW(name, NULL, 0);
    if (needed > 0) {
        wchar_t *val = (wchar_t *)malloc((size_t)needed * sizeof(wchar_t));
        if (!val) return false;
        DWORD got = GetEnvironmentVariableW(name, val, needed);
        if (got > 0 && got < needed) {
            *out_val = val;
            return true;
        }
        free(val);
    }

    if (!ctx || !ctx->extensions_enabled) return false;

    if (_wcsicmp(name, L"CD") == 0) {
        wchar_t cwd[MAX_PATH * 4];
        if (GetCurrentDirectoryW((DWORD)(sizeof(cwd) / sizeof(cwd[0])), cwd) > 0) {
            *out_val = _wcsdup(cwd);
            return *out_val != NULL;
        }
        return false;
    }
    if (_wcsicmp(name, L"ERRORLEVEL") == 0) {
        wchar_t buf[32];
        swprintf(buf, 32, L"%d", ctx->last_exit_code);
        *out_val = _wcsdup(buf);
        return *out_val != NULL;
    }
    if (_wcsicmp(name, L"CMDEXTVERSION") == 0) {
        *out_val = _wcsdup(L"2");
        return *out_val != NULL;
    }
    if (_wcsicmp(name, L"CMDCMDLINE") == 0) {
        *out_val = _wcsdup(ctx->cmd_cmdline[0] ? ctx->cmd_cmdline : ctx->self_path);
        return *out_val != NULL;
    }
    if (_wcsicmp(name, L"DATE") == 0) {
        wchar_t buf[128];
        if (GetDateFormatW(LOCALE_USER_DEFAULT, DATE_SHORTDATE, NULL, NULL, buf, 128) > 0) {
            *out_val = _wcsdup(buf);
            return *out_val != NULL;
        }
        return false;
    }
    if (_wcsicmp(name, L"TIME") == 0) {
        wchar_t buf[128];
        SYSTEMTIME st;
        GetLocalTime(&st);
        swprintf(buf, 128, L"%2u:%02u:%02u.%02u",
                 (unsigned)st.wHour, (unsigned)st.wMinute,
                 (unsigned)st.wSecond, (unsigned)(st.wMilliseconds / 10));
        *out_val = _wcsdup(buf);
        return *out_val != NULL;
    }
    if (_wcsicmp(name, L"RANDOM") == 0) {
        wchar_t buf[32];
        swprintf(buf, 32, L"%d", rand() % 32768);
        *out_val = _wcsdup(buf);
        return *out_val != NULL;
    }
    if (_wcsicmp(name, L"HIGHESTNUMANODENUMBER") == 0) {
        ULONG node = 0;
        GetNumaHighestNodeNumber(&node);
        wchar_t buf[32];
        swprintf(buf, 32, L"%lu", (unsigned long)node);
        *out_val = _wcsdup(buf);
        return *out_val != NULL;
    }

    return false;
}

static wchar_t *apply_var_modifier(const wchar_t *val, const wchar_t *mod) {
    if (!val) return _wcsdup(L"");
    if (!mod || !*mod) return _wcsdup(val);

    if (mod[0] == L'~') {
        const wchar_t *p = mod + 1;
        wchar_t *endp = NULL;
        long off = wcstol(p, &endp, 10);
        size_t vlen = wcslen(val);
        long start = off < 0 ? (long)vlen + off : off;
        if (start < 0) start = 0;
        if ((size_t)start >= vlen) return _wcsdup(L"");

        if (endp && *endp == L',') {
            long len = wcstol(endp + 1, NULL, 10);
            long count = len < 0 ? ((long)vlen - start + len) : len;
            if (count <= 0) return _wcsdup(L"");
            if ((size_t)(start + count) > vlen) count = (long)vlen - start;
            wchar_t *out = (wchar_t *)malloc(((size_t)count + 1) * sizeof(wchar_t));
            if (!out) return NULL;
            memcpy(out, val + start, (size_t)count * sizeof(wchar_t));
            out[count] = L'\0';
            return out;
        }
        return _wcsdup(val + start);
    }

    const wchar_t *eq = wcschr(mod, L'=');
    if (!eq) return _wcsdup(val);

    bool star = (mod[0] == L'*');
    const wchar_t *old_s = star ? mod + 1 : mod;
    size_t old_len = (size_t)(eq - old_s);
    const wchar_t *new_s = eq + 1;
    if (old_len == 0) return _wcsdup(val);

    wbuf_t out = { 0 };
    if (star) {
        const wchar_t *p = val;
        bool found = false;
        while (*p) {
            if (_wcsnicmp(p, old_s, old_len) == 0) {
                wbuf_append(&out, new_s);
                wbuf_append(&out, p + old_len);
                found = true;
                break;
            }
            p++;
        }
        if (!found) wbuf_append(&out, val);
        return wbuf_finish(&out);
    }

    const wchar_t *p = val;
    while (*p) {
        if (_wcsnicmp(p, old_s, old_len) == 0) {
            wbuf_append(&out, new_s);
            p += old_len;
        } else {
            wbuf_append_ch(&out, *p++);
        }
    }
    return wbuf_finish(&out);
}

static bool is_tilde_mod_char(wchar_t c) {
    wchar_t lc = towlower(c);
    return lc == L'f' || lc == L'd' || lc == L'p' || lc == L'n' ||
           lc == L'x' || lc == L's' || lc == L'a' || lc == L't' || lc == L'z';
}

wchar_t *cmd_apply_tilde_mods(const wchar_t *raw_arg, const wchar_t *mods) {
    if (!raw_arg) return _wcsdup(L"");

    /* Strip surrounding quotes */
    size_t rlen = wcslen(raw_arg);
    const wchar_t *start = raw_arg;
    if (rlen >= 2 && raw_arg[0] == L'"' && raw_arg[rlen - 1] == L'"') {
        start++;
        rlen -= 2;
    }
    wchar_t *unquoted = (wchar_t *)malloc((rlen + 1) * sizeof(wchar_t));
    if (!unquoted) return NULL;
    memcpy(unquoted, start, rlen * sizeof(wchar_t));
    unquoted[rlen] = L'\0';

    if (!mods || !*mods) return unquoted;

    bool want_f = false, want_d = false, want_p = false, want_n = false;
    bool want_x = false, want_s = false, want_a = false, want_t = false, want_z = false;
    for (const wchar_t *m = mods; *m; m++) {
        switch (towlower(*m)) {
            case L'f': want_f = true; break;
            case L'd': want_d = true; break;
            case L'p': want_p = true; break;
            case L'n': want_n = true; break;
            case L'x': want_x = true; break;
            case L's': want_s = true; break;
            case L'a': want_a = true; break;
            case L't': want_t = true; break;
            case L'z': want_z = true; break;
            default: break;
        }
    }

    bool any_path = want_f || want_d || want_p || want_n || want_x || want_s;
    bool any_meta = want_a || want_t || want_z;
    if (!any_path && !any_meta) return unquoted;

    wchar_t full[MAX_PATH * 4];
    if (GetFullPathNameW(unquoted, (DWORD)(sizeof(full) / sizeof(full[0])), full, NULL) == 0) {
        wcsncpy(full, unquoted, (sizeof(full) / sizeof(full[0])) - 1);
        full[(sizeof(full) / sizeof(full[0])) - 1] = L'\0';
    }

    if (want_s) {
        wchar_t short_path[MAX_PATH * 4];
        if (GetShortPathNameW(full, short_path, (DWORD)(sizeof(short_path) / sizeof(short_path[0]))) > 0) {
            wcscpy(full, short_path);
        }
    }

    wbuf_t out = { 0 };
    if (any_meta) {
        WIN32_FILE_ATTRIBUTE_DATA fad;
        if (GetFileAttributesExW(full, GetFileExInfoStandard, &fad)) {
            if (want_a) {
                DWORD a = fad.dwFileAttributes;
                wchar_t attr[10];
                attr[0] = (a & FILE_ATTRIBUTE_DIRECTORY) ? L'd' : L'-';
                attr[1] = (a & FILE_ATTRIBUTE_READONLY)  ? L'r' : L'-';
                attr[2] = (a & FILE_ATTRIBUTE_ARCHIVE)   ? L'a' : L'-';
                attr[3] = (a & FILE_ATTRIBUTE_HIDDEN)    ? L'h' : L'-';
                attr[4] = (a & FILE_ATTRIBUTE_SYSTEM)    ? L's' : L'-';
                attr[5] = L'-'; attr[6] = L'-'; attr[7] = L'-'; attr[8] = L'-'; attr[9] = L'\0';
                wbuf_append(&out, attr);
                wbuf_append_ch(&out, L' ');
            }
            if (want_t) {
                FILETIME lft;
                SYSTEMTIME st;
                FileTimeToLocalFileTime(&fad.ftLastWriteTime, &lft);
                FileTimeToSystemTime(&lft, &st);
                wchar_t tbuf[64];
                swprintf(tbuf, 64, L"%02u/%02u/%04u %02u:%02u ",
                         (unsigned)st.wMonth, (unsigned)st.wDay, (unsigned)st.wYear,
                         (unsigned)st.wHour, (unsigned)st.wMinute);
                wbuf_append(&out, tbuf);
            }
            if (want_z) {
                ULONGLONG sz = (((ULONGLONG)fad.nFileSizeHigh) << 32) | fad.nFileSizeLow;
                wchar_t zbuf[32];
                swprintf(zbuf, 32, L"%llu ", (unsigned long long)sz);
                wbuf_append(&out, zbuf);
            }
        }
    }

    if (want_f || (!any_path && any_meta)) {
        wbuf_append(&out, full);
    } else if (any_path) {
        /* Split drive, dir, fname, ext */
        wchar_t drive[16] = { 0 };
        wchar_t dir[MAX_PATH * 4] = { 0 };
        wchar_t fname[MAX_PATH] = { 0 };
        wchar_t ext[MAX_PATH] = { 0 };
        _wsplitpath_s(full, drive, 16, dir, MAX_PATH * 4, fname, MAX_PATH, ext, MAX_PATH);
        if (want_d) wbuf_append(&out, drive);
        if (want_p) wbuf_append(&out, dir);
        if (want_n) wbuf_append(&out, fname);
        if (want_x) wbuf_append(&out, ext);
        if (want_s && !want_d && !want_p && !want_n && !want_x) {
            wbuf_append(&out, full);
        }
    }

    free(unquoted);
    return wbuf_finish(&out);
}

static const wchar_t *get_batch_arg(const cmd_batch_frame_t *bf, int idx) {
    if (!bf || idx < 0 || idx >= bf->argc) return NULL;
    return bf->argv[idx];
}

wchar_t *cmd_expand_percent(const shell_ctx_t *ctx, const wchar_t *line, bool is_batch) {
    if (!line) return _wcsdup(L"");
    wbuf_t out = { 0 };
    const cmd_batch_frame_t *bf = (ctx && is_batch) ? (const cmd_batch_frame_t *)ctx->batch_frame : NULL;

    const wchar_t *p = line;
    while (*p) {
        if (*p != L'%') {
            wbuf_append_ch(&out, *p++);
            continue;
        }
        p++; /* skip '%' */
        if (!*p) {
            if (!is_batch) wbuf_append_ch(&out, L'%');
            break;
        }
        if (is_batch && *p == L'%') {
            wbuf_append_ch(&out, L'%');
            p++;
            continue;
        }
        if (is_batch && *p == L'*') {
            if (bf && bf->raw_tail) wbuf_append(&out, bf->raw_tail);
            p++;
            continue;
        }
        if (is_batch && *p >= L'0' && *p <= L'9') {
            int idx = *p - L'0';
            const wchar_t *a = get_batch_arg(bf, idx);
            if (a) wbuf_append(&out, a);
            p++;
            continue;
        }
        if (is_batch && *p == L'~') {
            const wchar_t *q = p + 1;
            wchar_t mods[32];
            size_t mi = 0;
            while (*q && is_tilde_mod_char(*q) && mi + 1 < 32) {
                mods[mi++] = *q++;
            }
            mods[mi] = L'\0';
            if (*q >= L'0' && *q <= L'9') {
                int idx = *q - L'0';
                const wchar_t *a = get_batch_arg(bf, idx);
                wchar_t *expanded = cmd_apply_tilde_mods(a ? a : L"", mods);
                if (expanded) {
                    wbuf_append(&out, expanded);
                    free(expanded);
                }
                p = q + 1;
                continue;
            }
        }

        /* Look for closing '%' */
        const wchar_t *close = wcschr(p, L'%');
        if (!close) {
            wbuf_append_ch(&out, L'%');
            continue;
        }

        size_t inner_len = (size_t)(close - p);
        if (inner_len == 0) {
            wbuf_append_ch(&out, L'%');
            p = close + 1;
            continue;
        }

        wchar_t *inner = (wchar_t *)malloc((inner_len + 1) * sizeof(wchar_t));
        if (!inner) break;
        memcpy(inner, p, inner_len * sizeof(wchar_t));
        inner[inner_len] = L'\0';

        const wchar_t *colon = wcschr(inner, L':');
        const wchar_t *mod = NULL;
        if (colon) {
            size_t nlen = (size_t)(colon - inner);
            wchar_t *vname = (wchar_t *)malloc((nlen + 1) * sizeof(wchar_t));
            if (vname) {
                memcpy(vname, inner, nlen * sizeof(wchar_t));
                vname[nlen] = L'\0';
                mod = colon + 1;
                wchar_t *val = NULL;
                if (cmd_lookup_var(ctx, vname, &val)) {
                    wchar_t *sub = apply_var_modifier(val, mod);
                    if (sub) {
                        wbuf_append(&out, sub);
                        free(sub);
                    }
                    free(val);
                } else if (!is_batch) {
                    wbuf_append_ch(&out, L'%');
                    wbuf_append(&out, inner);
                    wbuf_append_ch(&out, L'%');
                }
                free(vname);
            }
        } else {
            wchar_t *val = NULL;
            if (cmd_lookup_var(ctx, inner, &val)) {
                wbuf_append(&out, val);
                free(val);
            } else if (!is_batch) {
                wbuf_append_ch(&out, L'%');
                wbuf_append(&out, inner);
                wbuf_append_ch(&out, L'%');
            }
        }

        free(inner);
        p = close + 1;
    }

    return wbuf_finish(&out);
}

wchar_t *cmd_expand_delayed(const shell_ctx_t *ctx, const wchar_t *text) {
    if (!text) return _wcsdup(L"");
    if (!ctx || !ctx->delayed_expansion || !wcschr(text, L'!')) {
        return _wcsdup(text);
    }

    wbuf_t out = { 0 };
    const wchar_t *p = text;
    bool in_quotes = false;
    while (*p) {
        if (*p == L'"') {
            in_quotes = !in_quotes;
            wbuf_append_ch(&out, *p++);
            continue;
        }
        if (!in_quotes && *p == L'^' && p[1] == L'!') {
            wbuf_append_ch(&out, L'!');
            p += 2;
            continue;
        }
        if (*p != L'!') {
            wbuf_append_ch(&out, *p++);
            continue;
        }
        p++;
        const wchar_t *close = wcschr(p, L'!');
        if (!close) {
            wbuf_append_ch(&out, L'!');
            continue;
        }
        size_t inner_len = (size_t)(close - p);
        if (inner_len == 0) {
            p = close + 1;
            continue;
        }
        wchar_t *inner = (wchar_t *)malloc((inner_len + 1) * sizeof(wchar_t));
        if (!inner) break;
        memcpy(inner, p, inner_len * sizeof(wchar_t));
        inner[inner_len] = L'\0';

        const wchar_t *colon = wcschr(inner, L':');
        if (colon) {
            size_t nlen = (size_t)(colon - inner);
            wchar_t *vname = (wchar_t *)malloc((nlen + 1) * sizeof(wchar_t));
            if (vname) {
                memcpy(vname, inner, nlen * sizeof(wchar_t));
                vname[nlen] = L'\0';
                wchar_t *val = NULL;
                if (cmd_lookup_var(ctx, vname, &val)) {
                    wchar_t *sub = apply_var_modifier(val, colon + 1);
                    if (sub) {
                        wbuf_append(&out, sub);
                        free(sub);
                    }
                    free(val);
                }
                free(vname);
            }
        } else {
            wchar_t *val = NULL;
            if (cmd_lookup_var(ctx, inner, &val)) {
                wbuf_append(&out, val);
                free(val);
            }
        }
        free(inner);
        p = close + 1;
    }

    return wbuf_finish(&out);
}

static const wchar_t *lookup_for_var(const shell_ctx_t *ctx, wchar_t letter) {
    if (!ctx) return NULL;
    for (const cmd_for_var_t *fv = (const cmd_for_var_t *)ctx->for_vars; fv; fv = fv->prev) {
        if (fv->letter == letter) return fv->value;
    }
    return NULL;
}

wchar_t *cmd_expand_for_vars(const shell_ctx_t *ctx, const wchar_t *text) {
    if (!text) return _wcsdup(L"");
    if (!ctx || !ctx->for_vars || !wcschr(text, L'%')) {
        return _wcsdup(text);
    }

    wbuf_t out = { 0 };
    const wchar_t *p = text;
    while (*p) {
        if (*p != L'%') {
            wbuf_append_ch(&out, *p++);
            continue;
        }
        if (p[1] == L'~') {
            const wchar_t *q = p + 2;
            wchar_t mods[32];
            size_t mi = 0;
            while (*q && is_tilde_mod_char(*q) && mi + 1 < 32) {
                mods[mi++] = *q++;
            }
            mods[mi] = L'\0';
            if (iswalnum(*q)) {
                const wchar_t *fv = lookup_for_var(ctx, *q);
                if (fv) {
                    wchar_t *expanded = cmd_apply_tilde_mods(fv, mods);
                    if (expanded) {
                        wbuf_append(&out, expanded);
                        free(expanded);
                    }
                    p = q + 1;
                    continue;
                }
            }
        } else if (iswalnum(p[1])) {
            const wchar_t *fv = lookup_for_var(ctx, p[1]);
            if (fv) {
                wbuf_append(&out, fv);
                p += 2;
                continue;
            }
        }
        wbuf_append_ch(&out, *p++);
    }
    return wbuf_finish(&out);
}
