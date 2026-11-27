#ifndef PLUMBUS_CONSOLE_H
#define PLUMBUS_CONSOLE_H

#include <stdbool.h>
#include <stddef.h>
#include <wchar.h>

typedef struct {
    unsigned long orig_in_mode;
    unsigned long orig_out_mode;
    bool has_in_mode;
    bool has_out_mode;
} console_state_t;

bool console_init(console_state_t *st);
void console_enter_raw(const console_state_t *st);
void console_restore(const console_state_t *st);

bool console_write_w(const wchar_t *text, size_t len);
int console_get_width(void);
void console_clear_screen(void);

#endif /* PLUMBUS_CONSOLE_H */
