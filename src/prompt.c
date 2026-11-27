#include "prompt.h"
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void abbreviate_home(const wchar_t *cwd, wchar_t *out, size_t out_cap) {
    if (!cwd || !*cwd) {
        wcsncpy(out, L"~", out_cap - 1);
        out[out_cap - 1] = L'\0';
        return;
    }
    wchar_t home[MAX_PATH * 2];
    DWORD hl = GetEnvironmentVariableW(L"USERPROFILE", home, (DWORD)(sizeof(home) / sizeof(home[0])));
    if (hl > 0 && hl < sizeof(home) / sizeof(home[0])) {
        while (hl > 0 && home[hl - 1] == L'\\') home[--hl] = L'\0';
        size_t clen = wcslen(cwd);
        if (clen >= hl && _wcsnicmp(cwd, home, hl) == 0 &&
            (cwd[hl] == L'\0' || cwd[hl] == L'\\')) {
            swprintf(out, out_cap, L"~%ls", cwd + hl);
            return;
        }
    }
    wcsncpy(out, cwd, out_cap - 1);
    out[out_cap - 1] = L'\0';
}

wchar_t *prompt_render(const plumbus_config_t *cfg,
                       const wchar_t *cwd,
                       int last_exit_code,
                       const git_status_t *git) {
    wchar_t short_path[MAX_PATH * 4];
    abbreviate_home(cwd, short_path, sizeof(short_path) / sizeof(short_path[0]));

    plumbus_theme_t theme = cfg ? cfg->theme : THEME_ROBBYRUSSELL;
    bool nf = cfg ? cfg->nerd_fonts : true;

    size_t cap = wcslen(short_path) + 512;
    wchar_t *out = (wchar_t *)malloc(cap * sizeof(wchar_t));
    if (!out) return NULL;
    out[0] = L'\0';

    if (theme == THEME_AGNOSTER) {
        const wchar_t *sep = nf ? L"\uE0B0" : L">";
        const wchar_t *branch_glyph = nf ? L"\uE0A0 " : L"";
        if (last_exit_code != 0) {
            swprintf(out, cap, L"\x1b[48;2;255;85;85m\x1b[38;2;248;248;242m \u2717 %d \x1b[48;2;40;100;200m\x1b[38;2;255;85;85m%ls",
                     last_exit_code, sep);
        } else {
            wcscpy(out, L"\x1b[48;2;40;100;200m");
        }
        wchar_t path_seg[MAX_PATH * 4 + 64];
        swprintf(path_seg, sizeof(path_seg) / sizeof(path_seg[0]),
                 L"\x1b[38;2;255;255;255m %ls ", short_path);
        wcscat(out, path_seg);

        if (git && git->in_repo) {
            const wchar_t *bg = git->is_dirty ? L"\x1b[48;2;241;250;140m" : L"\x1b[48;2;80;250;123m";
            const wchar_t *fg_arrow = git->is_dirty ? L"\x1b[38;2;241;250;140m" : L"\x1b[38;2;80;250;123m";
            const wchar_t *mark = git->is_dirty ? L"\u2717" : L"\u2714";
            wchar_t git_seg[512];
            swprintf(git_seg, sizeof(git_seg) / sizeof(git_seg[0]),
                     L"%ls\x1b[38;2;40;100;200m%ls\x1b[38;2;20;20;20m %ls%ls %ls \x1b[0m%ls%ls\x1b[0m ",
                     bg, sep, branch_glyph, git->branch, mark, fg_arrow, sep);
            wcscat(out, git_seg);
        } else {
            wchar_t end_seg[64];
            swprintf(end_seg, 64, L"\x1b[0m\x1b[38;2;40;100;200m%ls\x1b[0m ", sep);
            wcscat(out, end_seg);
        }
        return out;
    }

    /* Default: robbyrussell */
    const wchar_t *arrow_col = (last_exit_code == 0) ? L"\x1b[38;2;80;250;123m" : L"\x1b[38;2;255;85;85m";
    const wchar_t *arrow = L"\u279C"; /* ➜ */

    if (git && git->in_repo) {
        const wchar_t *mark = git->is_dirty
            ? L"\x1b[38;2;241;250;140m\u2717"
            : L"\x1b[38;2;80;250;123m\u2714";
        swprintf(out, cap,
                 L"%ls%ls\x1b[0m \x1b[38;2;139;233;253m%ls\x1b[0m \x1b[38;2;98;114;164mgit:(\x1b[38;2;255;85;85m%ls\x1b[38;2;98;114;164m)\x1b[0m %ls\x1b[0m ",
                 arrow_col, arrow, short_path, git->branch, mark);
    } else {
        swprintf(out, cap,
                 L"%ls%ls\x1b[0m \x1b[38;2;139;233;253m%ls\x1b[0m ",
                 arrow_col, arrow, short_path);
    }
    return out;
}

size_t prompt_visible_width(const wchar_t *ansi_str) {
    if (!ansi_str) return 0;
    size_t w = 0;
    const wchar_t *p = ansi_str;
    while (*p) {
        if (*p == L'\x1b' && p[1] == L'[') {
            p += 2;
            while (*p && !((*p >= L'@' && *p <= L'~'))) p++;
            if (*p) p++;
            continue;
        }
        w++;
        p++;
    }
    return w;
}
