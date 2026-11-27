#include "config.h"
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void config_reset_defaults(plumbus_config_t *cfg) {
    if (!cfg) return;
    cfg->theme = THEME_ROBBYRUSSELL;
    cfg->nerd_fonts = true;
    cfg->syntax_highlighting = true;
    cfg->ghost_text = true;
    cfg->git_status = true;
}

void config_init(plumbus_config_t *cfg) {
    if (!cfg) return;
    memset(cfg, 0, sizeof(*cfg));
    config_reset_defaults(cfg);
}

void config_free(plumbus_config_t *cfg) {
    if (!cfg) return;
    for (size_t i = 0; i < cfg->alias_count; i++) {
        free(cfg->aliases[i].name);
        free(cfg->aliases[i].value);
    }
    free(cfg->aliases);
    cfg->aliases = NULL;
    cfg->alias_count = 0;
    cfg->alias_cap = 0;
}

bool config_copy(plumbus_config_t *dst, const plumbus_config_t *src) {
    if (!dst || !src) return false;
    config_free(dst);
    config_init(dst);
    dst->theme = src->theme;
    dst->nerd_fonts = src->nerd_fonts;
    dst->syntax_highlighting = src->syntax_highlighting;
    dst->ghost_text = src->ghost_text;
    dst->git_status = src->git_status;
    for (size_t i = 0; i < src->alias_count; i++) {
        if (!config_set_alias(dst, src->aliases[i].name, src->aliases[i].value)) {
            return false;
        }
    }
    return true;
}

bool config_set_alias(plumbus_config_t *cfg, const wchar_t *name, const wchar_t *value) {
    if (!cfg || !name || !*name || !value) return false;
    for (size_t i = 0; i < cfg->alias_count; i++) {
        if (_wcsicmp(cfg->aliases[i].name, name) == 0) {
            wchar_t *nv = _wcsdup(value);
            if (!nv) return false;
            free(cfg->aliases[i].value);
            cfg->aliases[i].value = nv;
            return true;
        }
    }
    if (cfg->alias_count == cfg->alias_cap) {
        size_t ncap = cfg->alias_cap ? cfg->alias_cap * 2 : 16;
        plumbus_alias_t *nb = (plumbus_alias_t *)realloc(cfg->aliases, ncap * sizeof(plumbus_alias_t));
        if (!nb) return false;
        cfg->aliases = nb;
        cfg->alias_cap = ncap;
    }
    cfg->aliases[cfg->alias_count].name = _wcsdup(name);
    cfg->aliases[cfg->alias_count].value = _wcsdup(value);
    if (!cfg->aliases[cfg->alias_count].name || !cfg->aliases[cfg->alias_count].value) {
        free(cfg->aliases[cfg->alias_count].name);
        free(cfg->aliases[cfg->alias_count].value);
        return false;
    }
    cfg->alias_count++;
    return true;
}

bool config_remove_alias(plumbus_config_t *cfg, const wchar_t *name) {
    if (!cfg || !name) return false;
    for (size_t i = 0; i < cfg->alias_count; i++) {
        if (_wcsicmp(cfg->aliases[i].name, name) == 0) {
            free(cfg->aliases[i].name);
            free(cfg->aliases[i].value);
            memmove(&cfg->aliases[i], &cfg->aliases[i + 1],
                    (cfg->alias_count - 1 - i) * sizeof(plumbus_alias_t));
            cfg->alias_count--;
            return true;
        }
    }
    return false;
}

const wchar_t *config_find_alias(const plumbus_config_t *cfg, const wchar_t *name) {
    if (!cfg || !name || !*name) return NULL;
    for (size_t i = 0; i < cfg->alias_count; i++) {
        if (_wcsicmp(cfg->aliases[i].name, name) == 0) {
            return cfg->aliases[i].value;
        }
    }
    return NULL;
}

wchar_t *config_expand_alias_line(const plumbus_config_t *cfg, const wchar_t *line) {
    if (!cfg || !line) return NULL;
    const wchar_t *p = line;
    while (*p == L' ' || *p == L'\t') p++;
    if (!*p) return NULL;

    const wchar_t *ts = p;
    while (*p && *p != L' ' && *p != L'\t' && *p != L'|' && *p != L'&') p++;
    size_t tlen = (size_t)(p - ts);
    if (tlen == 0 || tlen >= 256) return NULL;

    wchar_t tok[256];
    memcpy(tok, ts, tlen * sizeof(wchar_t));
    tok[tlen] = L'\0';

    const wchar_t *val = config_find_alias(cfg, tok);
    if (!val) return NULL;

    size_t vlen = wcslen(val);
    size_t rlen = wcslen(p);
    wchar_t *out = (wchar_t *)malloc((vlen + rlen + 1) * sizeof(wchar_t));
    if (!out) return NULL;
    memcpy(out, val, vlen * sizeof(wchar_t));
    memcpy(out + vlen, p, (rlen + 1) * sizeof(wchar_t));
    return out;
}

static wchar_t *trim_w(wchar_t *s) {
    while (*s == L' ' || *s == L'\t') s++;
    size_t n = wcslen(s);
    while (n > 0 && (s[n - 1] == L' ' || s[n - 1] == L'\t' || s[n - 1] == L'\r' || s[n - 1] == L'\n')) {
        s[--n] = L'\0';
    }
    return s;
}

static void strip_matching_quotes(wchar_t *s) {
    size_t n = wcslen(s);
    if (n >= 2 && ((s[0] == L'"' && s[n - 1] == L'"') || (s[0] == L'\'' && s[n - 1] == L'\''))) {
        memmove(s, s + 1, (n - 2) * sizeof(wchar_t));
        s[n - 2] = L'\0';
    }
}

static bool parse_bool_val(const wchar_t *val) {
    return (_wcsicmp(val, L"on") == 0 || _wcsicmp(val, L"true") == 0 || wcscmp(val, L"1") == 0);
}

static bool resolve_rc_path(wchar_t *out_path, size_t out_cap) {
    DWORD n = GetEnvironmentVariableW(L"PLUMBUS_RC_FILE", out_path, (DWORD)out_cap);
    if (n > 0 && n < out_cap) return true;

    wchar_t home[MAX_PATH * 2];
    DWORD hl = GetEnvironmentVariableW(L"USERPROFILE", home, (DWORD)(sizeof(home) / sizeof(home[0])));
    if (hl == 0 || hl >= sizeof(home) / sizeof(home[0])) return false;
    swprintf(out_path, out_cap, L"%ls\\.cmdrc", home);
    return true;
}

bool config_load_rc(plumbus_config_t *cfg) {
    if (!cfg) return false;
    wchar_t rc_path[MAX_PATH * 4];
    if (!resolve_rc_path(rc_path, sizeof(rc_path) / sizeof(rc_path[0]))) return true;

    HANDLE f = CreateFileW(rc_path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE) return true; /* Optional file */

    LARGE_INTEGER sz;
    if (!GetFileSizeEx(f, &sz) || sz.QuadPart <= 0 || sz.QuadPart > (1024 * 1024)) {
        CloseHandle(f);
        return true;
    }

    char *utf8 = (char *)malloc((size_t)sz.QuadPart + 1);
    if (!utf8) { CloseHandle(f); return false; }
    DWORD rd = 0;
    BOOL ok = ReadFile(f, utf8, (DWORD)sz.QuadPart, &rd, NULL);
    CloseHandle(f);
    if (!ok) { free(utf8); return false; }
    utf8[rd] = '\0';

    int wneed = MultiByteToWideChar(CP_UTF8, 0, utf8, (int)rd, NULL, 0);
    if (wneed <= 0) { free(utf8); return true; }
    wchar_t *wbuf = (wchar_t *)malloc(((size_t)wneed + 1) * sizeof(wchar_t));
    if (!wbuf) { free(utf8); return false; }
    MultiByteToWideChar(CP_UTF8, 0, utf8, (int)rd, wbuf, wneed);
    wbuf[wneed] = L'\0';
    free(utf8);

    wchar_t *p = wbuf;
    while (*p) {
        wchar_t *eol = wcspbrk(p, L"\r\n");
        if (eol) *eol = L'\0';
        wchar_t *line = trim_w(p);

        if (line[0] != L'\0' && line[0] != L'#') {
            if (_wcsnicmp(line, L"alias", 5) == 0 && (line[5] == L' ' || line[5] == L'\t')) {
                wchar_t *rest = trim_w(line + 5);
                wchar_t *eq = wcschr(rest, L'=');
                if (eq) {
                    *eq = L'\0';
                    wchar_t *name = trim_w(rest);
                    wchar_t *val = trim_w(eq + 1);
                    strip_matching_quotes(val);
                    config_set_alias(cfg, name, val);
                }
            } else if (_wcsnicmp(line, L"theme", 5) == 0 && (line[5] == L'=' || line[5] == L' ' || line[5] == L'\t')) {
                wchar_t *val = trim_w(line + 6);
                strip_matching_quotes(val);
                if (_wcsicmp(val, L"agnoster") == 0) cfg->theme = THEME_AGNOSTER;
                else if (_wcsicmp(val, L"robbyrussell") == 0) cfg->theme = THEME_ROBBYRUSSELL;
            } else if (_wcsnicmp(line, L"nerd_fonts", 10) == 0 && (line[10] == L'=' || line[10] == L' ' || line[10] == L'\t')) {
                cfg->nerd_fonts = parse_bool_val(trim_w(line + 11));
            } else if (_wcsnicmp(line, L"syntax_highlighting", 19) == 0 && (line[19] == L'=' || line[19] == L' ' || line[19] == L'\t')) {
                cfg->syntax_highlighting = parse_bool_val(trim_w(line + 20));
            } else if (_wcsnicmp(line, L"ghost_text", 10) == 0 && (line[10] == L'=' || line[10] == L' ' || line[10] == L'\t')) {
                cfg->ghost_text = parse_bool_val(trim_w(line + 11));
            } else if (_wcsnicmp(line, L"git_status", 10) == 0 && (line[10] == L'=' || line[10] == L' ' || line[10] == L'\t')) {
                cfg->git_status = parse_bool_val(trim_w(line + 11));
            } else if (_wcsnicmp(line, L"set", 3) == 0 && (line[3] == L' ' || line[3] == L'\t')) {
                wchar_t *rest = trim_w(line + 3);
                wchar_t *eq = wcschr(rest, L'=');
                if (eq) {
                    *eq = L'\0';
                    wchar_t *name = trim_w(rest);
                    wchar_t *val = trim_w(eq + 1);
                    SetEnvironmentVariableW(name, val[0] ? val : NULL);
                }
            }
        }

        if (!eol) break;
        p = eol + 1;
        while (*p == L'\r' || *p == L'\n') p++;
    }

    free(wbuf);
    return true;
}

static bool write_utf8_str(HANDLE f, const wchar_t *wstr) {
    int need = WideCharToMultiByte(CP_UTF8, 0, wstr, -1, NULL, 0, NULL, NULL);
    if (need <= 1) return true;
    char *buf = (char *)malloc((size_t)need);
    if (!buf) return false;
    WideCharToMultiByte(CP_UTF8, 0, wstr, -1, buf, need, NULL, NULL);
    DWORD wr = 0;
    BOOL ok = WriteFile(f, buf, (DWORD)(need - 1), &wr, NULL);
    free(buf);
    return ok != 0;
}

bool config_save(const plumbus_config_t *cfg) {
    if (!cfg) return false;
    wchar_t rc_path[MAX_PATH * 4];
    if (!resolve_rc_path(rc_path, sizeof(rc_path) / sizeof(rc_path[0]))) return false;

    HANDLE f = CreateFileW(rc_path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE) return false;

    wchar_t header[1024];
    swprintf(header, sizeof(header) / sizeof(header[0]),
             L"# Plumbus Configuration (~\\.cmdrc)\r\n"
             L"# Managed by Plumbus Configurator (plumbus config / F2 / Ctrl+,)\r\n\r\n"
             L"# Prompt & Visual\r\n"
             L"theme=%ls\r\n"
             L"nerd_fonts=%ls\r\n"
             L"git_status=%ls\r\n\r\n"
             L"# Interactive Line Editor\r\n"
             L"syntax_highlighting=%ls\r\n"
             L"ghost_text=%ls\r\n",
             cfg->theme == THEME_AGNOSTER ? L"agnoster" : L"robbyrussell",
             cfg->nerd_fonts ? L"on" : L"off",
             cfg->git_status ? L"on" : L"off",
             cfg->syntax_highlighting ? L"on" : L"off",
             cfg->ghost_text ? L"on" : L"off");

    bool ok = write_utf8_str(f, header);

    if (ok && cfg->alias_count > 0) {
        ok = write_utf8_str(f, L"\r\n# Aliases\r\n");
        for (size_t i = 0; ok && i < cfg->alias_count; i++) {
            wchar_t line[1024];
            swprintf(line, sizeof(line) / sizeof(line[0]),
                     L"alias %ls=\"%ls\"\r\n", cfg->aliases[i].name, cfg->aliases[i].value);
            ok = write_utf8_str(f, line);
        }
    }

    CloseHandle(f);
    return ok;
}
