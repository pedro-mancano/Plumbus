#ifndef PLUMBUS_LINE_EDITOR_H
#define PLUMBUS_LINE_EDITOR_H

#include "history.h"
#include "highlight.h"
#include "completion.h"
#include <stdbool.h>
#include <stddef.h>
#include <wchar.h>

typedef struct {
    wchar_t *buf;
    size_t len;
    size_t cap;
    size_t cursor;
} line_buf_t;

void line_buf_init(line_buf_t *lb);
void line_buf_free(line_buf_t *lb);
void line_buf_clear(line_buf_t *lb);
bool line_buf_set(line_buf_t *lb, const wchar_t *text);
bool line_buf_insert_char(line_buf_t *lb, wchar_t ch);
bool line_buf_insert_str(line_buf_t *lb, const wchar_t *s);
void line_buf_backspace(line_buf_t *lb);
void line_buf_delete(line_buf_t *lb);
void line_buf_move_left(line_buf_t *lb);
void line_buf_move_right(line_buf_t *lb);
void line_buf_move_word_left(line_buf_t *lb);
void line_buf_move_word_right(line_buf_t *lb);
void line_buf_move_home(line_buf_t *lb);
void line_buf_move_end(line_buf_t *lb);
void line_buf_kill_word_back(line_buf_t *lb);
void line_buf_kill_to_start(line_buf_t *lb);
void line_buf_kill_to_end(line_buf_t *lb);

typedef enum {
    LINE_READ_OK = 0,
    LINE_READ_EOF,
    LINE_READ_ABORT
} line_read_status_t;

typedef bool (*line_editor_config_cb_fn)(void *user_data,
                                         wchar_t **new_prompt_ansi,
                                         size_t *new_prompt_vis_len,
                                         bool *out_enable_hl,
                                         bool *out_enable_ghost);

line_read_status_t line_editor_read(const wchar_t *prompt_ansi,
                                    size_t prompt_visible_len,
                                    history_t *hist,
                                    highlight_alias_lookup_fn alias_fn,
                                    void *alias_user_data,
                                    bool enable_highlight,
                                    bool enable_ghost_text,
                                    line_editor_config_cb_fn cfg_cb,
                                    void *cfg_user_data,
                                    wchar_t **out_line);

#endif /* PLUMBUS_LINE_EDITOR_H */
