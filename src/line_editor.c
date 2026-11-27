#include "line_editor.h"
#include "console.h"
#include "prompt.h"
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wctype.h>

void line_buf_init(line_buf_t *lb) {
    lb->cap = 256;
    lb->buf = (wchar_t *)malloc(lb->cap * sizeof(wchar_t));
    if (lb->buf) lb->buf[0] = L'\0';
    lb->len = 0;
    lb->cursor = 0;
}

void line_buf_free(line_buf_t *lb) {
    free(lb->buf);
    lb->buf = NULL;
    lb->len = 0;
    lb->cap = 0;
    lb->cursor = 0;
}

void line_buf_clear(line_buf_t *lb) {
    lb->len = 0;
    lb->cursor = 0;
    if (lb->buf) lb->buf[0] = L'\0';
}

static bool ensure_cap(line_buf_t *lb, size_t need) {
    if (need + 1 <= lb->cap) return true;
    size_t ncap = lb->cap ? lb->cap * 2 : 256;
    while (ncap < need + 1) ncap *= 2;
    wchar_t *nb = (wchar_t *)realloc(lb->buf, ncap * sizeof(wchar_t));
    if (!nb) return false;
    lb->buf = nb;
    lb->cap = ncap;
    return true;
}

bool line_buf_set(line_buf_t *lb, const wchar_t *text) {
    if (!text) text = L"";
    size_t n = wcslen(text);
    if (!ensure_cap(lb, n)) return false;
    memcpy(lb->buf, text, (n + 1) * sizeof(wchar_t));
    lb->len = n;
    lb->cursor = n;
    return true;
}

bool line_buf_insert_char(line_buf_t *lb, wchar_t ch) {
    if (!ensure_cap(lb, lb->len + 1)) return false;
    memmove(lb->buf + lb->cursor + 1, lb->buf + lb->cursor, (lb->len - lb->cursor + 1) * sizeof(wchar_t));
    lb->buf[lb->cursor] = ch;
    lb->len++;
    lb->cursor++;
    return true;
}

bool line_buf_insert_str(line_buf_t *lb, const wchar_t *s) {
    if (!s || !*s) return true;
    size_t slen = wcslen(s);
    if (!ensure_cap(lb, lb->len + slen)) return false;
    memmove(lb->buf + lb->cursor + slen, lb->buf + lb->cursor, (lb->len - lb->cursor + 1) * sizeof(wchar_t));
    memcpy(lb->buf + lb->cursor, s, slen * sizeof(wchar_t));
    lb->len += slen;
    lb->cursor += slen;
    return true;
}

void line_buf_backspace(line_buf_t *lb) {
    if (lb->cursor == 0) return;
    memmove(lb->buf + lb->cursor - 1, lb->buf + lb->cursor, (lb->len - lb->cursor + 1) * sizeof(wchar_t));
    lb->cursor--;
    lb->len--;
}

void line_buf_delete(line_buf_t *lb) {
    if (lb->cursor >= lb->len) return;
    memmove(lb->buf + lb->cursor, lb->buf + lb->cursor + 1, (lb->len - lb->cursor) * sizeof(wchar_t));
    lb->len--;
}

void line_buf_move_left(line_buf_t *lb) {
    if (lb->cursor > 0) lb->cursor--;
}

void line_buf_move_right(line_buf_t *lb) {
    if (lb->cursor < lb->len) lb->cursor++;
}

static bool is_word_sep(wchar_t c) {
    return c == L' ' || c == L'\t' || c == L'\\' || c == L'/' || c == L'-' || c == L'.';
}

void line_buf_move_word_left(line_buf_t *lb) {
    while (lb->cursor > 0 && is_word_sep(lb->buf[lb->cursor - 1])) lb->cursor--;
    while (lb->cursor > 0 && !is_word_sep(lb->buf[lb->cursor - 1])) lb->cursor--;
}

void line_buf_move_word_right(line_buf_t *lb) {
    while (lb->cursor < lb->len && !is_word_sep(lb->buf[lb->cursor])) lb->cursor++;
    while (lb->cursor < lb->len && is_word_sep(lb->buf[lb->cursor])) lb->cursor++;
}

void line_buf_move_home(line_buf_t *lb) {
    lb->cursor = 0;
}

void line_buf_move_end(line_buf_t *lb) {
    lb->cursor = lb->len;
}

void line_buf_kill_word_back(line_buf_t *lb) {
    size_t end = lb->cursor;
    while (lb->cursor > 0 && (lb->buf[lb->cursor - 1] == L' ' || lb->buf[lb->cursor - 1] == L'\t')) {
        lb->cursor--;
    }
    while (lb->cursor > 0 && lb->buf[lb->cursor - 1] != L' ' && lb->buf[lb->cursor - 1] != L'\t') {
        lb->cursor--;
    }
    size_t removed = end - lb->cursor;
    if (removed > 0) {
        memmove(lb->buf + lb->cursor, lb->buf + end, (lb->len - end + 1) * sizeof(wchar_t));
        lb->len -= removed;
    }
}

void line_buf_kill_to_start(line_buf_t *lb) {
    if (lb->cursor == 0) return;
    memmove(lb->buf, lb->buf + lb->cursor, (lb->len - lb->cursor + 1) * sizeof(wchar_t));
    lb->len -= lb->cursor;
    lb->cursor = 0;
}

void line_buf_kill_to_end(line_buf_t *lb) {
    lb->len = lb->cursor;
    lb->buf[lb->len] = L'\0';
}

bool line_editor_is_config_shortcut(unsigned short vk, wchar_t ch, unsigned long ctrl_key_state) {
    bool ctrl = (ctrl_key_state & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED)) != 0;
    return vk == VK_F2 || (ctrl && (vk == VK_OEM_COMMA || ch == L','));
}

wchar_t *line_editor_format_frame(const wchar_t *prompt_ansi,
                                  size_t prompt_visible_len,
                                  const line_buf_t *lb,
                                  const wchar_t *ghost,
                                  bool enable_highlight,
                                  highlight_alias_lookup_fn alias_fn,
                                  void *alias_user_data) {
    wchar_t *colored = enable_highlight
        ? highlight_render_line(lb->buf, ghost, alias_fn, alias_user_data)
        : NULL;
    const wchar_t *body = colored ? colored : lb->buf;
    size_t ghost_len = (!colored && ghost) ? wcslen(ghost) : 0;
    size_t total_cap = wcslen(prompt_ansi) + wcslen(body) + ghost_len + 128;
    wchar_t *frame = (wchar_t *)malloc(total_cap * sizeof(wchar_t));
    if (!frame) {
        free(colored);
        return NULL;
    }

    size_t target_col = prompt_visible_len + lb->cursor + 1;
    if (!colored && ghost && *ghost) {
        swprintf(frame, total_cap, L"\r%ls%ls\x1b[38;2;110;110;110m%ls\x1b[0m\x1b[K\r\x1b[%zuG",
                 prompt_ansi, body, ghost, target_col);
    } else {
        swprintf(frame, total_cap, L"\r%ls%ls\x1b[K\r\x1b[%zuG",
                 prompt_ansi, body, target_col);
    }
    free(colored);
    return frame;
}

static void redraw_line(const wchar_t *prompt_ansi,
                        size_t prompt_visible_len,
                        const line_buf_t *lb,
                        const wchar_t *ghost,
                        bool enable_highlight,
                        highlight_alias_lookup_fn alias_fn,
                        void *alias_user_data) {
    wchar_t *frame = line_editor_format_frame(prompt_ansi, prompt_visible_len, lb, ghost,
                                              enable_highlight, alias_fn, alias_user_data);
    if (!frame) return;
    console_write_w(frame, wcslen(frame));
    free(frame);
}

static void redraw_reverse_search(const line_buf_t *query, const wchar_t *match) {
    size_t cap = query->len + (match ? wcslen(match) : 0) + 128;
    wchar_t *frame = (wchar_t *)malloc(cap * sizeof(wchar_t));
    if (!frame) return;
    swprintf(frame, cap, L"\r(reverse-i-search)'%ls': %ls\x1b[K",
             query->buf, match ? match : L"");
    console_write_w(frame, wcslen(frame));
    free(frame);
}

static void split_prompt(const wchar_t *full, wchar_t **out_header, wchar_t **out_input) {
    const wchar_t *src = full ? full : L"> ";
    const wchar_t *nl = wcsrchr(src, L'\n');
    if (!nl) {
        *out_header = NULL;
        *out_input = _wcsdup(src);
        return;
    }
    size_t hlen = (size_t)(nl - src);
    *out_header = (wchar_t *)malloc((hlen + 1) * sizeof(wchar_t));
    if (*out_header) {
        if (hlen > 0) memcpy(*out_header, src, hlen * sizeof(wchar_t));
        (*out_header)[hlen] = L'\0';
    }
    *out_input = _wcsdup(nl + 1);
}

static void emit_prompt_header(const wchar_t *header) {
    if (!header || !*header) return;
    console_write_w(header, wcslen(header));
    console_write_w(L"\r\n", 2);
}

line_read_status_t line_editor_read(const wchar_t *prompt_ansi,
                                    size_t prompt_visible_len,
                                    history_t *hist,
                                    highlight_alias_lookup_fn alias_fn,
                                    void *alias_user_data,
                                    bool enable_highlight,
                                    bool enable_ghost_text,
                                    line_editor_config_cb_fn cfg_cb,
                                    void *cfg_user_data,
                                    wchar_t **out_line) {
    (void)prompt_visible_len;
    *out_line = NULL;
    HANDLE hIn = GetStdHandle(STD_INPUT_HANDLE);

    wchar_t *cur_header = NULL;
    wchar_t *cur_prompt = NULL;
    split_prompt(prompt_ansi, &cur_header, &cur_prompt);
    if (!cur_prompt) cur_prompt = _wcsdup(L"> ");
    size_t cur_pvis = prompt_last_line_visible_width(cur_prompt);
    bool hl_on = enable_highlight;
    bool ghost_on = enable_ghost_text;

    line_buf_t lb;
    line_buf_init(&lb);

    line_buf_t draft;
    line_buf_init(&draft);

    size_t hist_pos = hist ? hist->len : 0;
    bool in_search = false;
    line_buf_t search_q;
    line_buf_init(&search_q);
    ptrdiff_t search_idx = hist ? (ptrdiff_t)hist->len - 1 : -1;

    completion_list_t comp = { 0 };
    size_t comp_idx = 0;
    bool comp_active = false;

    const wchar_t *ghost = NULL;
    emit_prompt_header(cur_header);
    redraw_line(cur_prompt, cur_pvis, &lb, ghost, hl_on, alias_fn, alias_user_data);

    line_read_status_t status = LINE_READ_OK;
    for (;;) {
        INPUT_RECORD rec;
        DWORD rd = 0;
        if (!ReadConsoleInputW(hIn, &rec, 1, &rd) || rd == 0) {
            status = LINE_READ_EOF;
            break;
        }
        if (rec.EventType == WINDOW_BUFFER_SIZE_EVENT) {
            if (!in_search) redraw_line(cur_prompt, cur_pvis, &lb, ghost, hl_on, alias_fn, alias_user_data);
            continue;
        }
        if (rec.EventType != KEY_EVENT || !rec.Event.KeyEvent.bKeyDown) continue;

        KEY_EVENT_RECORD *ke = &rec.Event.KeyEvent;
        WORD vk = ke->wVirtualKeyCode;
        wchar_t ch = ke->uChar.UnicodeChar;
        bool ctrl = (ke->dwControlKeyState & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED)) != 0;
        bool shift = (ke->dwControlKeyState & SHIFT_PRESSED) != 0;

        /* F2 or Ctrl+, opens Plumbus Configurator preserving current line buffer */
        if (cfg_cb && line_editor_is_config_shortcut(vk, ch, ke->dwControlKeyState)) {
            wchar_t *new_p = NULL;
            size_t new_vis = cur_pvis;
            if (cfg_cb(cfg_user_data, &new_p, &new_vis, &hl_on, &ghost_on) && new_p) {
                free(cur_header);
                free(cur_prompt);
                cur_header = NULL;
                cur_prompt = NULL;
                split_prompt(new_p, &cur_header, &cur_prompt);
                free(new_p);
                if (!cur_prompt) cur_prompt = _wcsdup(L"> ");
                cur_pvis = prompt_last_line_visible_width(cur_prompt);
            }
            emit_prompt_header(cur_header);
            ghost = (ghost_on && lb.cursor == lb.len) ? history_find_ghost_suffix(hist, lb.buf) : NULL;
            redraw_line(cur_prompt, cur_pvis, &lb, ghost, hl_on, alias_fn, alias_user_data);
            continue;
        }

        /* Ctrl+C */
        if (ctrl && (vk == 'C' || ch == 3)) {
            console_write_w(L"^C\r\n", 4);
            line_buf_clear(&lb);
            status = LINE_READ_ABORT;
            break;
        }

        /* Reverse search mode (Ctrl+R) */
        if (in_search) {
            if (ctrl && vk == 'R') {
                if (search_idx > 0) {
                    ptrdiff_t next = history_search_reverse(hist, search_q.buf, search_idx - 1);
                    if (next >= 0) {
                        search_idx = next;
                        const wchar_t *m = history_get(hist, (size_t)search_idx);
                        if (m) line_buf_set(&lb, m);
                    }
                }
                redraw_reverse_search(&search_q, lb.buf);
                continue;
            }
            if (vk == VK_RETURN) {
                in_search = false;
                redraw_line(cur_prompt, cur_pvis, &lb, NULL, hl_on, alias_fn, alias_user_data);
                console_write_w(L"\r\n", 2);
                status = LINE_READ_OK;
                break;
            }
            if (vk == VK_ESCAPE || vk == VK_LEFT || vk == VK_RIGHT) {
                in_search = false;
                ghost = (ghost_on && lb.cursor == lb.len) ? history_find_ghost_suffix(hist, lb.buf) : NULL;
                redraw_line(cur_prompt, cur_pvis, &lb, ghost, hl_on, alias_fn, alias_user_data);
                continue;
            }
            if (vk == VK_BACK) {
                line_buf_backspace(&search_q);
            } else if (ch >= 32 && !ctrl) {
                line_buf_insert_char(&search_q, ch);
            }
            search_idx = history_search_reverse(hist, search_q.buf, hist ? (ptrdiff_t)hist->len - 1 : -1);
            if (search_idx >= 0) {
                const wchar_t *m = history_get(hist, (size_t)search_idx);
                if (m) line_buf_set(&lb, m);
            }
            redraw_reverse_search(&search_q, search_idx >= 0 ? lb.buf : L"");
            continue;
        }

        /* Reset tab completion on non-Tab key */
        if (vk != VK_TAB && comp_active) {
            completion_free(&comp);
            comp_active = false;
        }

        if (vk == VK_RETURN || ch == L'\r') {
            redraw_line(cur_prompt, cur_pvis, &lb, NULL, hl_on, alias_fn, alias_user_data);
            console_write_w(L"\r\n", 2);
            status = LINE_READ_OK;
            break;
        }
        if (ctrl && (vk == 'D' || ch == 4) && lb.len == 0) {
            console_write_w(L"\r\n", 2);
            status = LINE_READ_EOF;
            break;
        }

        if (ctrl && vk == 'R') {
            in_search = true;
            line_buf_clear(&search_q);
            search_idx = hist ? (ptrdiff_t)hist->len - 1 : -1;
            redraw_reverse_search(&search_q, lb.buf);
            continue;
        }

        if (vk == VK_TAB || ch == L'\t') {
            if (!comp_active) {
                completion_generate(lb.buf, lb.cursor, &comp);
                if (comp.count > 0) {
                    comp_active = true;
                    comp_idx = shift ? (comp.count - 1) : 0;
                }
            } else {
                if (shift) {
                    comp_idx = (comp_idx == 0) ? (comp.count - 1) : (comp_idx - 1);
                } else {
                    comp_idx = (comp_idx + 1) % comp.count;
                }
            }
            if (comp_active && comp.count > 0) {
                const wchar_t *rep = comp.items[comp_idx];
                size_t rlen = wcslen(rep);
                size_t tail_len = lb.len - (comp.replace_start + comp.replace_len);
                wchar_t *new_line = (wchar_t *)malloc((comp.replace_start + rlen + tail_len + 1) * sizeof(wchar_t));
                if (new_line) {
                    memcpy(new_line, lb.buf, comp.replace_start * sizeof(wchar_t));
                    memcpy(new_line + comp.replace_start, rep, rlen * sizeof(wchar_t));
                    memcpy(new_line + comp.replace_start + rlen,
                           lb.buf + comp.replace_start + comp.replace_len,
                           (tail_len + 1) * sizeof(wchar_t));
                    line_buf_set(&lb, new_line);
                    lb.cursor = comp.replace_start + rlen;
                    comp.replace_len = rlen;
                    free(new_line);
                }
            }
        } else if ((ctrl && vk == 'A') || vk == VK_HOME) {
            line_buf_move_home(&lb);
        } else if ((ctrl && vk == 'E') || vk == VK_END) {
            if (vk == VK_END && ghost && lb.cursor == lb.len) {
                line_buf_insert_str(&lb, ghost);
            } else {
                line_buf_move_end(&lb);
            }
        } else if (ctrl && vk == 'W') {
            line_buf_kill_word_back(&lb);
        } else if (ctrl && vk == 'U') {
            line_buf_kill_to_start(&lb);
        } else if (ctrl && vk == 'K') {
            line_buf_kill_to_end(&lb);
        } else if (ctrl && vk == 'L') {
            console_clear_screen();
            emit_prompt_header(cur_header);
        } else if (ctrl && vk == VK_LEFT) {
            line_buf_move_word_left(&lb);
        } else if (ctrl && vk == VK_RIGHT) {
            line_buf_move_word_right(&lb);
        } else if (vk == VK_LEFT) {
            line_buf_move_left(&lb);
        } else if (vk == VK_RIGHT) {
            if (lb.cursor == lb.len && ghost) {
                line_buf_insert_str(&lb, ghost);
            } else {
                line_buf_move_right(&lb);
            }
        } else if (vk == VK_UP) {
            if (hist && hist->len > 0 && hist_pos > 0) {
                if (hist_pos == hist->len) line_buf_set(&draft, lb.buf);
                hist_pos--;
                line_buf_set(&lb, history_get(hist, hist_pos));
            }
        } else if (vk == VK_DOWN) {
            if (hist && hist_pos < hist->len) {
                hist_pos++;
                if (hist_pos == hist->len) {
                    line_buf_set(&lb, draft.buf);
                } else {
                    line_buf_set(&lb, history_get(hist, hist_pos));
                }
            }
        } else if (vk == VK_BACK || ch == 8) {
            line_buf_backspace(&lb);
        } else if (vk == VK_DELETE) {
            line_buf_delete(&lb);
        } else if (ch >= 32 && !ctrl) {
            line_buf_insert_char(&lb, ch);
        }

        ghost = (ghost_on && lb.cursor == lb.len) ? history_find_ghost_suffix(hist, lb.buf) : NULL;
        redraw_line(cur_prompt, cur_pvis, &lb, ghost, hl_on, alias_fn, alias_user_data);
    }

    if (status == LINE_READ_OK) {
        *out_line = _wcsdup(lb.buf);
    }
    free(cur_header);
    free(cur_prompt);
    completion_free(&comp);
    line_buf_free(&search_q);
    line_buf_free(&draft);
    line_buf_free(&lb);
    return status;
}
