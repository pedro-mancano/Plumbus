#ifndef PLUMBUS_CMD_MSG_H
#define PLUMBUS_CMD_MSG_H

#include <windows.h>
#include <wchar.h>

enum {
    CMD_MSG_BAD_PARM           = 9001,
    CMD_MSG_SYNTAX_ERR         = 9002,
    CMD_MSG_NO_LABEL           = 9008,
    CMD_MSG_NOT_RECOGNIZED     = 9009,
    CMD_MSG_FILES_COPIED       = 9014,
    CMD_MSG_DIR_OF             = 9017,
    CMD_MSG_ECHO_OFF           = 9029,
    CMD_MSG_ECHO_ON            = 9030,
    CMD_MSG_VERIFY_OFF         = 9031,
    CMD_MSG_VERIFY_ON          = 9032,
    CMD_MSG_COPY_SELF          = 9033,
    CMD_MSG_UNEXPECTED         = 9034,
    CMD_MSG_DUP_FILE           = 9037,
    CMD_MSG_VER_BANNER         = 9040,
    CMD_MSG_VOL_SERIAL         = 9051,
    CMD_MSG_DIR_EXISTS         = 9052,
    CMD_MSG_VOL_NO_LABEL       = 9054,
    CMD_MSG_VOL_LABEL          = 9055,
    CMD_MSG_KEYS_ON            = 9056,
    CMD_MSG_KEYS_OFF           = 9057,
    CMD_MSG_FILES_MOVED        = 9070,
    CMD_MSG_INVALID_SWITCH     = 9077,
    CMD_MSG_ASSOC_NOT_FOUND    = 1073750947,
    CMD_MSG_FTYPE_NOT_FOUND    = 1073750949,
    CMD_MSG_INVALID_SETLOCAL   = 1073750950,
    CMD_MSG_ENV_NOT_DEFINED    = 1073750953,
    CMD_MSG_CALL_LABEL_OUTSIDE = 1073750954,
    CMD_MSG_LABEL_NOT_FOUND    = 1073750955,
    CMD_MSG_INVALID_SHIFT      = 1073750987,
    CMD_MSG_ARITH_PAREN        = 1073750988,
    CMD_MSG_ARITH_OPERAND      = 1073750989,
    CMD_MSG_ARITH_OPERATOR     = 1073750990,
    CMD_MSG_ARITH_BAD_NUM      = 1073750991,
    CMD_MSG_ARITH_32BIT        = 1073750992,
    CMD_MSG_ARITH_DIV_ZERO     = 1073750993,
    CMD_MSG_FILE_NOT_FOUND     = 1073751825,
    CMD_MSG_COULD_NOT_FIND     = 1073751826,
    CMD_MSG_COLOR_HELP         = 1073751843,
    CMD_MSG_MKLINK_HELP        = 1073751850,
    CMD_MSG_NO_PRIV            = 1073751857
};

/*
 * Formats a Win32 error code using FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM)
 * and writes it to h.
 */
void cmd_msg_win32(HANDLE h, DWORD win32_err);

/*
 * Formats a cmd.exe message ID via read-only MUI resource lookup (with English fallback)
 * and writes it to h.
 */
void cmd_msg_print(HANDLE h, DWORD msg_id, const wchar_t *fallback,
                   const wchar_t *arg1, const wchar_t *arg2);

/*
 * Formats a cmd.exe message ID into out_buf (used when returning parse errors).
 */
void cmd_msg_format_buf(DWORD msg_id, const wchar_t *fallback,
                        const wchar_t *arg1, const wchar_t *arg2,
                        wchar_t *out_buf, size_t out_cap);

/*
 * Formats the localized "Microsoft Windows [Version ...]" banner line (without trailing CRLF).
 */
void cmd_format_ver_banner(wchar_t *out_buf, size_t out_cap);

/*
 * Prints the authentic cmd.exe startup header (version + copyright + blank line) to h.
 */
void cmd_print_startup_header(HANDLE h);

#endif /* PLUMBUS_CMD_MSG_H */
