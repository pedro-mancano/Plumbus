#include "console.h"
#include <windows.h>

bool console_init(console_state_t *st) {
    if (!st) return false;
    st->orig_in_mode = 0;
    st->orig_out_mode = 0;
    st->has_in_mode = false;
    st->has_out_mode = false;

    HANDLE hIn = GetStdHandle(STD_INPUT_HANDLE);
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);

    DWORD mode = 0;
    if (hIn != INVALID_HANDLE_VALUE && GetConsoleMode(hIn, &mode)) {
        st->orig_in_mode = mode;
        st->has_in_mode = true;
    }
    if (hOut != INVALID_HANDLE_VALUE && GetConsoleMode(hOut, &mode)) {
        st->orig_out_mode = mode;
        st->has_out_mode = true;
        SetConsoleMode(hOut, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING | ENABLE_PROCESSED_OUTPUT);
    }
    SetConsoleCP(CP_UTF8);
    SetConsoleOutputCP(CP_UTF8);
    return st->has_in_mode && st->has_out_mode;
}

void console_enter_raw(const console_state_t *st) {
    (void)st;
    HANDLE hIn = GetStdHandle(STD_INPUT_HANDLE);
    if (hIn != INVALID_HANDLE_VALUE) {
        SetConsoleMode(hIn, ENABLE_WINDOW_INPUT);
    }
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hOut != INVALID_HANDLE_VALUE && st && st->has_out_mode) {
        SetConsoleMode(hOut, st->orig_out_mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING | ENABLE_PROCESSED_OUTPUT);
    }
}

void console_restore(const console_state_t *st) {
    if (!st) return;
    if (st->has_in_mode) {
        HANDLE hIn = GetStdHandle(STD_INPUT_HANDLE);
        if (hIn != INVALID_HANDLE_VALUE) SetConsoleMode(hIn, st->orig_in_mode);
    }
    if (st->has_out_mode) {
        HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
        if (hOut != INVALID_HANDLE_VALUE) SetConsoleMode(hOut, st->orig_out_mode);
    }
}

bool console_write_w(const wchar_t *text, size_t len) {
    if (!text || len == 0) return true;
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hOut == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    return WriteConsoleW(hOut, text, (DWORD)len, &written, NULL) != 0;
}

int console_get_width(void) {
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (hOut != INVALID_HANDLE_VALUE && GetConsoleScreenBufferInfo(hOut, &csbi)) {
        int w = (int)(csbi.srWindow.Right - csbi.srWindow.Left + 1);
        return w > 0 ? w : 80;
    }
    return 80;
}

void console_clear_screen(void) {
    const wchar_t seq[] = L"\x1b[2J\x1b[H";
    console_write_w(seq, sizeof(seq) / sizeof(seq[0]) - 1);
}
