#ifndef PLUMBUS_PROXY_H
#define PLUMBUS_PROXY_H

#include <stdbool.h>
#include <wchar.h>

/*
 * Returns true if STD_INPUT_HANDLE is an interactive console (FILE_TYPE_CHAR + GetConsoleMode).
 */
bool proxy_is_stdin_interactive(void);

/*
 * Transparently delegates execution to backend_cmd_path with raw_after_exe,
 * inheriting standard handles and propagating exact ExitCode.
 */
int proxy_delegate_raw(const wchar_t *backend_cmd_path, const wchar_t *raw_after_exe);

#endif /* PLUMBUS_PROXY_H */
