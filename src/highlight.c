#include "highlight.h"
#include "resolver.h"
#include <stdlib.h>
#include <string.h>

#define COL_GREEN   L"\x1b[38;2;80;250;123m"
#define COL_RED     L"\x1b[38;2;255;85;85m"
#define COL_CYAN    L"\x1b[38;2;139;233;253m"
#define COL_YELLOW  L"\x1b[38;2;241;250;140m"
#define COL_MAGENTA L"\x1b[38;2;255;121;198m"
#define COL_GRAY    L"\x1b[38;2;110;110;110m"
#define COL_RESET   L"\x1b[0m"

typedef struct {
    wchar_t *buf;
    size_t len;
    size_t cap;
} wbuf_t;

static void wbuf_append(wbuf_t *b, const wchar_t *s, size_t n) {
    if (b->len + n + 1 > b->cap) {
        size_t ncap = b->cap ? b->cap * 2 : 256;
        while (ncap < b->len + n + 1) ncap *= 2;
        wchar_t *nb = (wchar_t *)realloc(b->buf, ncap * sizeof(wchar_t));
        if (!nb) return;
        b->buf = nb;
        b->cap = ncap;
    }
    memcpy(b->buf + b->len, s, n * sizeof(wchar_t));
    b->len += n;
    b->buf[b->len] = L'\0';
}

static void wbuf_append_str(wbuf_t *b, const wchar_t *s) {
    wbuf_append(b, s, wcslen(s));
}

wchar_t *highlight_render_line(const wchar_t *line,
                               const wchar_t *ghost_suffix,
                               highlight_alias_lookup_fn alias_fn,
                               void *alias_user_data) {
    wbuf_t out = { NULL, 0, 0 };
    wbuf_append(&out, L"", 0);

    if (line && *line) {
        const wchar_t *p = line;
        bool expect_cmd = true;

        while (*p) {
            if (*p == L' ' || *p == L'\t') {
                const wchar_t *ws = p;
                while (*p == L' ' || *p == L'\t') p++;
                wbuf_append(&out, ws, (size_t)(p - ws));
                continue;
            }

            /* Shell operators */
            if (*p == L'|' || *p == L'&' || *p == L'>' || *p == L'<') {
                const wchar_t *op = p;
                if ((p[0] == L'&' && p[1] == L'&') ||
                    (p[0] == L'|' && p[1] == L'|') ||
                    (p[0] == L'>' && p[1] == L'>')) {
                    p += 2;
                } else {
                    p += 1;
                }
                wbuf_append_str(&out, COL_MAGENTA);
                wbuf_append(&out, op, (size_t)(p - op));
                wbuf_append_str(&out, COL_RESET);
                if (*op == L'|' || *op == L'&') {
                    expect_cmd = true;
                }
                continue;
            }

            /* Quoted string */
            if (*p == L'"') {
                const wchar_t *qs = p;
                p++;
                while (*p && *p != L'"') p++;
                if (*p == L'"') p++;
                if (expect_cmd) {
                    size_t inner_len = (size_t)(p - qs);
                    wchar_t tok[1024];
                    size_t tlen = 0;
                    for (size_t i = 0; i < inner_len && tlen + 1 < 1024; i++) {
                        if (qs[i] != L'"') tok[tlen++] = qs[i];
                    }
                    tok[tlen] = L'\0';
                    bool valid = (alias_fn && alias_fn(tok, alias_user_data)) || resolver_command_exists(tok);
                    wbuf_append_str(&out, valid ? COL_GREEN : COL_RED);
                    wbuf_append(&out, qs, (size_t)(p - qs));
                    wbuf_append_str(&out, COL_RESET);
                    expect_cmd = false;
                } else {
                    wbuf_append_str(&out, COL_YELLOW);
                    wbuf_append(&out, qs, (size_t)(p - qs));
                    wbuf_append_str(&out, COL_RESET);
                }
                continue;
            }

            /* Regular token */
            const wchar_t *ts = p;
            while (*p && *p != L' ' && *p != L'\t' && *p != L'|' && *p != L'&' && *p != L'>' && *p != L'<') {
                if (*p == L'"') {
                    p++;
                    while (*p && *p != L'"') p++;
                    if (*p == L'"') p++;
                } else {
                    p++;
                }
            }
            size_t tlen = (size_t)(p - ts);

            if (expect_cmd) {
                wchar_t tok[1024];
                size_t clen = tlen < 1023 ? tlen : 1023;
                memcpy(tok, ts, clen * sizeof(wchar_t));
                tok[clen] = L'\0';
                bool valid = (alias_fn && alias_fn(tok, alias_user_data)) || resolver_command_exists(tok);
                wbuf_append_str(&out, valid ? COL_GREEN : COL_RED);
                wbuf_append(&out, ts, tlen);
                wbuf_append_str(&out, COL_RESET);
                expect_cmd = false;
            } else if (ts[0] == L'-' || ts[0] == L'/') {
                wbuf_append_str(&out, COL_CYAN);
                wbuf_append(&out, ts, tlen);
                wbuf_append_str(&out, COL_RESET);
            } else {
                wbuf_append(&out, ts, tlen);
            }
        }
    }

    if (ghost_suffix && *ghost_suffix) {
        wbuf_append_str(&out, COL_GRAY);
        wbuf_append_str(&out, ghost_suffix);
        wbuf_append_str(&out, COL_RESET);
    }

    return out.buf;
}
