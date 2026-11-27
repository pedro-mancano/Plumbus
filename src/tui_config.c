#include "tui_config.h"
#include "console.h"
#include "prompt.h"
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wctype.h>

void tui_cfg_state_init(tui_cfg_state_t *st, const plumbus_config_t *src) {
    if (!st) return;
    memset(st, 0, sizeof(*st));
    config_init(&st->draft);
    if (src) {
        config_copy(&st->draft, src);
    }
    st->selected = 0;
}

void tui_cfg_state_free(tui_cfg_state_t *st) {
    if (!st) return;
    config_free(&st->draft);
}

void tui_cfg_move_up(tui_cfg_state_t *st) {
    if (!st) return;
    if (st->in_alias_panel) {
        if (st->draft.alias_count > 0) {
            st->alias_selected = (st->alias_selected <= 0)
                ? (int)st->draft.alias_count - 1
                : st->alias_selected - 1;
        }
    } else {
        st->selected = (st->selected <= 0) ? (TUI_ITEM_COUNT - 1) : (st->selected - 1);
    }
}

void tui_cfg_move_down(tui_cfg_state_t *st) {
    if (!st) return;
    if (st->in_alias_panel) {
        if (st->draft.alias_count > 0) {
            st->alias_selected = (st->alias_selected + 1) % (int)st->draft.alias_count;
        }
    } else {
        st->selected = (st->selected + 1) % TUI_ITEM_COUNT;
    }
}

void tui_cfg_toggle_selected(tui_cfg_state_t *st) {
    if (!st || st->in_alias_panel) return;
    switch (st->selected) {
        case TUI_ITEM_THEME:
            st->draft.theme = (st->draft.theme == THEME_ROBBYRUSSELL) ? THEME_AGNOSTER : THEME_ROBBYRUSSELL;
            break;
        case TUI_ITEM_NERD_FONTS:
            st->draft.nerd_fonts = !st->draft.nerd_fonts;
            break;
        case TUI_ITEM_SYNTAX_HL:
            st->draft.syntax_highlighting = !st->draft.syntax_highlighting;
            break;
        case TUI_ITEM_GHOST_TEXT:
            st->draft.ghost_text = !st->draft.ghost_text;
            break;
        case TUI_ITEM_GIT_STATUS:
            st->draft.git_status = !st->draft.git_status;
            break;
        case TUI_ITEM_ALIASES:
            st->in_alias_panel = true;
            st->adding_alias = false;
            st->alias_selected = 0;
            break;
        default:
            break;
    }
}

void tui_cfg_restore_defaults(tui_cfg_state_t *st) {
    if (!st) return;
    config_reset_defaults(&st->draft);
    wcscpy(st->status_msg, L"Padrões restaurados (pressione [S] para salvar).");
}

bool tui_cfg_add_alias_expr(tui_cfg_state_t *st, const wchar_t *expr) {
    if (!st || !expr) return false;
    const wchar_t *eq = wcschr(expr, L'=');
    if (!eq || eq == expr) {
        wcscpy(st->status_msg, L"Formato inválido. Use: nome=comando");
        return false;
    }
    size_t nlen = (size_t)(eq - expr);
    if (nlen >= 128) return false;
    wchar_t name[128];
    memcpy(name, expr, nlen * sizeof(wchar_t));
    name[nlen] = L'\0';
    while (nlen > 0 && (name[nlen - 1] == L' ' || name[nlen - 1] == L'\t')) name[--nlen] = L'\0';
    const wchar_t *val = eq + 1;
    while (*val == L' ' || *val == L'\t') val++;
    if (!name[0] || !*val) {
        wcscpy(st->status_msg, L"Nome e comando são obrigatórios.");
        return false;
    }
    bool ok = config_set_alias(&st->draft, name, val);
    if (ok) {
        swprintf(st->status_msg, 128, L"Alias '%ls' adicionado.", name);
    }
    return ok;
}

bool tui_cfg_remove_selected_alias(tui_cfg_state_t *st) {
    if (!st || st->draft.alias_count == 0) return false;
    if (st->alias_selected < 0 || (size_t)st->alias_selected >= st->draft.alias_count) return false;
    wchar_t name[128];
    wcsncpy(name, st->draft.aliases[st->alias_selected].name, 127);
    name[127] = L'\0';
    bool ok = config_remove_alias(&st->draft, name);
    if (ok) {
        if (st->alias_selected >= (int)st->draft.alias_count && st->alias_selected > 0) {
            st->alias_selected--;
        }
        swprintf(st->status_msg, 128, L"Alias '%ls' removido.", name);
    }
    return ok;
}

typedef struct {
    wchar_t *buf;
    size_t len;
    size_t cap;
} tbuf_t;

static void tb_add(tbuf_t *b, const wchar_t *s) {
    size_t n = wcslen(s);
    if (b->len + n + 1 > b->cap) {
        size_t ncap = b->cap ? b->cap * 2 : 4096;
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

static void tb_repeat(tbuf_t *b, wchar_t ch, int count) {
    if (count <= 0) return;
    wchar_t tmp[256];
    int chunk = count < 255 ? count : 255;
    for (int i = 0; i < chunk; i++) tmp[i] = ch;
    tmp[chunk] = L'\0';
    while (count > 0) {
        int n = count < 255 ? count : 255;
        tmp[n] = L'\0';
        tb_add(b, tmp);
        count -= n;
    }
}

static void tb_goto(tbuf_t *b, int row, int col) {
    wchar_t seq[32];
    swprintf(seq, 32, L"\x1b[%d;%dH", row, col);
    tb_add(b, seq);
}

static void draw_box_row(tbuf_t *b, int row, int left_col, int inner_w, const wchar_t *content_ansi) {
    tb_goto(b, row, left_col);
    tb_add(b, L"\x1b[38;2;98;114;164m\u2502\x1b[0m ");
    tb_add(b, content_ansi);
    size_t vis = prompt_visible_width(content_ansi);
    int pad = (inner_w - 2) - (int)vis;
    if (pad > 0) tb_repeat(b, L' ', pad);
    tb_add(b, L" \x1b[38;2;98;114;164m\u2502\x1b[0m");
}

static void render_tui(const tui_cfg_state_t *st, const wchar_t *cwd, int last_exit_code, const git_status_t *git_st) {
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    int term_w = 80, term_h = 24;
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (hOut != INVALID_HANDLE_VALUE && GetConsoleScreenBufferInfo(hOut, &csbi)) {
        term_w = (int)(csbi.srWindow.Right - csbi.srWindow.Left + 1);
        term_h = (int)(csbi.srWindow.Bottom - csbi.srWindow.Top + 1);
    }

    int box_w = term_w >= 78 ? 74 : (term_w - 4);
    if (box_w < 54) box_w = 54;
    int inner_w = box_w - 2;
    int left = (term_w > box_w) ? ((term_w - box_w) / 2 + 1) : 1;
    int box_h = 19;
    int top = (term_h > box_h) ? ((term_h - box_h) / 2 + 1) : 1;

    tbuf_t tb = { NULL, 0, 0 };
    tb_add(&tb, L"\x1b[2J\x1b[H");

    tb_goto(&tb, top, left);
    const wchar_t *title = L" Plumbus Configurator ";
    int tlen = (int)wcslen(title);
    int left_dash = (inner_w - tlen) / 2;
    int right_dash = inner_w - tlen - left_dash;
    tb_add(&tb, L"\x1b[38;2;98;114;164m\u256D");
    tb_repeat(&tb, L'\u2500', left_dash);
    tb_add(&tb, L"\x1b[1;38;2;139;233;253m");
    tb_add(&tb, title);
    tb_add(&tb, L"\x1b[0;38;2;98;114;164m");
    tb_repeat(&tb, L'\u2500', right_dash);
    tb_add(&tb, L"\u256E\x1b[0m");

    int r = top + 1;

    draw_box_row(&tb, r++, left, inner_w, L"\x1b[38;2;189;147;249mLive Preview:\x1b[0m");

    git_status_t preview_git;
    if (git_st && git_st->in_repo) {
        preview_git = *git_st;
    } else {
        memset(&preview_git, 0, sizeof(preview_git));
        preview_git.in_repo = true;
        preview_git.is_dirty = false;
        wcscpy(preview_git.branch, L"main");
    }

    wchar_t *p_ansi = prompt_render(&st->draft, cwd ? cwd : L"C:\\Projects\\Plumbus",
                                    last_exit_code, st->draft.git_status ? &preview_git : NULL);
    wchar_t preview_line[2048];
    const wchar_t *cmd_sample = st->draft.syntax_highlighting
        ? L"\x1b[38;2;80;250;123mgit\x1b[0m status"
        : L"git status";
    const wchar_t *ghost_sample = st->draft.ghost_text
        ? L"\x1b[38;2;110;110;110m --short\x1b[0m"
        : L"";
    swprintf(preview_line, sizeof(preview_line) / sizeof(preview_line[0]),
             L"%ls%ls%ls", p_ansi ? p_ansi : L"> ", cmd_sample, ghost_sample);
    free(p_ansi);
    draw_box_row(&tb, r++, left, inner_w, preview_line);

    tb_goto(&tb, r++, left);
    tb_add(&tb, L"\x1b[38;2;98;114;164m\u251C");
    tb_repeat(&tb, L'\u2500', inner_w);
    tb_add(&tb, L"\u2524\x1b[0m");

    if (!st->in_alias_panel) {
        draw_box_row(&tb, r++, left, inner_w, L"");

        wchar_t alias_val[64];
        swprintf(alias_val, 64, L"[%zu configurado(s) -> Enter]", st->draft.alias_count);

        const wchar_t *labels[TUI_ITEM_COUNT] = {
            L"Tema do Prompt",
            L"Nerd Fonts (Powerline/Git)",
            L"Syntax Highlighting",
            L"Sugestões Inline (Ghost Text)",
            L"Status Git no Prompt",
            L"Gerenciar Aliases"
        };
        const wchar_t *values[TUI_ITEM_COUNT] = {
            st->draft.theme == THEME_AGNOSTER ? L"agnoster" : L"robbyrussell",
            st->draft.nerd_fonts ? L"ON" : L"OFF",
            st->draft.syntax_highlighting ? L"ON" : L"OFF",
            st->draft.ghost_text ? L"ON" : L"OFF",
            st->draft.git_status ? L"ON" : L"OFF",
            alias_val
        };

        for (int i = 0; i < TUI_ITEM_COUNT; i++) {
            wchar_t row_buf[512];
            bool sel = (i == st->selected);
            const wchar_t *val_col = L"\x1b[38;2;139;233;253m";
            if (wcscmp(values[i], L"ON") == 0) val_col = L"\x1b[38;2;80;250;123m";
            else if (wcscmp(values[i], L"OFF") == 0) val_col = L"\x1b[38;2;255;85;85m";

            if (sel) {
                swprintf(row_buf, 512, L"\x1b[48;2;68;71;90m\x1b[38;2;248;248;242m \u25B6 %-33ls %ls%-24ls \x1b[0m",
                         labels[i], val_col, values[i]);
            } else {
                swprintf(row_buf, 512, L"   %-33ls %ls%-24ls\x1b[0m",
                         labels[i], val_col, values[i]);
            }
            draw_box_row(&tb, r++, left, inner_w, row_buf);
        }

        draw_box_row(&tb, r++, left, inner_w, L"");
        draw_box_row(&tb, r++, left, inner_w,
                     L"\x1b[38;2;110;110;110mUse \u2191/\u2193 para navegar | \u2190/\u2192, Espaço ou Enter para alternar\x1b[0m");
    } else {
        draw_box_row(&tb, r++, left, inner_w,
                     L"\x1b[1;38;2;241;250;140mGerenciador de Aliases\x1b[0m  \x1b[38;2;110;110;110m([A] Adicionar | [D] Remover | [Esc] Voltar)\x1b[0m");

        if (st->adding_alias) {
            wchar_t inp[600];
            swprintf(inp, 600, L"\x1b[38;2;80;250;123mNovo alias (nome=cmd):\x1b[0m %ls_", st->alias_input);
            draw_box_row(&tb, r++, left, inner_w, inp);
        } else {
            draw_box_row(&tb, r++, left, inner_w, L"");
        }

        int shown = 0;
        if (st->draft.alias_count == 0) {
            draw_box_row(&tb, r++, left, inner_w, L"\x1b[38;2;110;110;110m  (Nenhum alias configurado — pressione [A] para criar)\x1b[0m");
            shown = 1;
        } else {
            size_t start = 0;
            if (st->alias_selected >= 6) start = (size_t)(st->alias_selected - 5);
            for (size_t i = start; i < st->draft.alias_count && shown < 6; i++, shown++) {
                wchar_t arow[512];
                if ((int)i == st->alias_selected) {
                    swprintf(arow, 512, L"\x1b[48;2;68;71;90m \u25B6 %-14ls = %-40ls \x1b[0m",
                             st->draft.aliases[i].name, st->draft.aliases[i].value);
                } else {
                    swprintf(arow, 512, L"   %-14ls = %-40ls",
                             st->draft.aliases[i].name, st->draft.aliases[i].value);
                }
                draw_box_row(&tb, r++, left, inner_w, arow);
            }
        }
        while (shown < 6) {
            draw_box_row(&tb, r++, left, inner_w, L"");
            shown++;
        }
        draw_box_row(&tb, r++, left, inner_w, L"");
    }

    if (st->status_msg[0]) {
        wchar_t sline[200];
        swprintf(sline, 200, L"\x1b[38;2;241;250;140m%ls\x1b[0m", st->status_msg);
        draw_box_row(&tb, r++, left, inner_w, sline);
    } else {
        draw_box_row(&tb, r++, left, inner_w, L"");
    }

    tb_goto(&tb, r++, left);
    tb_add(&tb, L"\x1b[38;2;98;114;164m\u251C");
    tb_repeat(&tb, L'\u2500', inner_w);
    tb_add(&tb, L"\u2524\x1b[0m");

    draw_box_row(&tb, r++, left, inner_w,
                 L"\x1b[38;2;80;250;123m[S]\x1b[0m Salvar no .cmdrc  \x1b[38;2;98;114;164m|\x1b[0m  "
                 L"\x1b[38;2;241;250;140m[R]\x1b[0m Restaurar Padrões  \x1b[38;2;98;114;164m|\x1b[0m  "
                 L"\x1b[38;2;255;85;85m[Esc/Q]\x1b[0m Cancelar");

    tb_goto(&tb, r, left);
    tb_add(&tb, L"\x1b[38;2;98;114;164m\u2570");
    tb_repeat(&tb, L'\u2500', inner_w);
    tb_add(&tb, L"\u256F\x1b[0m");

    if (tb.buf) {
        console_write_w(tb.buf, tb.len);
        free(tb.buf);
    }
}

bool tui_cfg_run(plumbus_config_t *live_cfg,
                 const wchar_t *cwd,
                 int last_exit_code,
                 const git_status_t *git_st) {
    if (!live_cfg) return false;

    tui_cfg_state_t st;
    tui_cfg_state_init(&st, live_cfg);

    const wchar_t enter_alt[] = L"\x1b[?1049h\x1b[?25l";
    console_write_w(enter_alt, wcslen(enter_alt));

    HANDLE hIn = GetStdHandle(STD_INPUT_HANDLE);
    bool saved = false;
    bool running = true;

    render_tui(&st, cwd, last_exit_code, git_st);

    while (running) {
        INPUT_RECORD rec;
        DWORD rd = 0;
        if (!ReadConsoleInputW(hIn, &rec, 1, &rd) || rd == 0) break;
        if (rec.EventType == WINDOW_BUFFER_SIZE_EVENT) {
            render_tui(&st, cwd, last_exit_code, git_st);
            continue;
        }
        if (rec.EventType != KEY_EVENT || !rec.Event.KeyEvent.bKeyDown) continue;

        KEY_EVENT_RECORD *ke = &rec.Event.KeyEvent;
        WORD vk = ke->wVirtualKeyCode;
        wchar_t ch = ke->uChar.UnicodeChar;
        st.status_msg[0] = L'\0';

        if (st.in_alias_panel && st.adding_alias) {
            if (vk == VK_ESCAPE) {
                st.adding_alias = false;
                st.alias_input[0] = L'\0';
                st.alias_input_len = 0;
            } else if (vk == VK_RETURN || ch == L'\r') {
                if (tui_cfg_add_alias_expr(&st, st.alias_input)) {
                    st.adding_alias = false;
                    st.alias_input[0] = L'\0';
                    st.alias_input_len = 0;
                }
            } else if (vk == VK_BACK || ch == 8) {
                if (st.alias_input_len > 0) {
                    st.alias_input[--st.alias_input_len] = L'\0';
                }
            } else if (ch >= 32 && st.alias_input_len + 1 < sizeof(st.alias_input) / sizeof(st.alias_input[0])) {
                st.alias_input[st.alias_input_len++] = ch;
                st.alias_input[st.alias_input_len] = L'\0';
            }
            render_tui(&st, cwd, last_exit_code, git_st);
            continue;
        }

        if (st.in_alias_panel) {
            if (vk == VK_ESCAPE) {
                st.in_alias_panel = false;
            } else if (vk == VK_UP) {
                tui_cfg_move_up(&st);
            } else if (vk == VK_DOWN) {
                tui_cfg_move_down(&st);
            } else if (towlower(ch) == L'a') {
                st.adding_alias = true;
                st.alias_input[0] = L'\0';
                st.alias_input_len = 0;
            } else if (towlower(ch) == L'd' || vk == VK_DELETE) {
                tui_cfg_remove_selected_alias(&st);
            } else if (towlower(ch) == L's') {
                if (config_save(&st.draft)) {
                    config_copy(live_cfg, &st.draft);
                    saved = true;
                    running = false;
                } else {
                    wcscpy(st.status_msg, L"Erro ao salvar .cmdrc.");
                }
            }
            if (running) render_tui(&st, cwd, last_exit_code, git_st);
            continue;
        }

        if (vk == VK_ESCAPE || towlower(ch) == L'q') {
            running = false;
        } else if (vk == VK_UP) {
            tui_cfg_move_up(&st);
        } else if (vk == VK_DOWN) {
            tui_cfg_move_down(&st);
        } else if (vk == VK_LEFT || vk == VK_RIGHT || vk == VK_SPACE || vk == VK_RETURN || ch == L'\r') {
            tui_cfg_toggle_selected(&st);
        } else if (towlower(ch) == L'r') {
            tui_cfg_restore_defaults(&st);
        } else if (towlower(ch) == L's') {
            if (config_save(&st.draft)) {
                config_copy(live_cfg, &st.draft);
                saved = true;
                running = false;
            } else {
                wcscpy(st.status_msg, L"Erro ao salvar .cmdrc.");
            }
        }

        if (running) render_tui(&st, cwd, last_exit_code, git_st);
    }

    const wchar_t leave_alt[] = L"\x1b[?25h\x1b[?1049l";
    console_write_w(leave_alt, wcslen(leave_alt));

    tui_cfg_state_free(&st);
    return saved;
}
