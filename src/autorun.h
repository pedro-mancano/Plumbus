#ifndef PLUMBUS_AUTORUN_H
#define PLUMBUS_AUTORUN_H

#include <stdbool.h>
#include <wchar.h>

/*
 * Reads AutoRun values (strictly KEY_READ) from HKLM then HKCU
 * Software\Microsoft\Command Processor\AutoRun.
 * Returns NULL if disable_autorun is true or no AutoRun command exists.
 * Caller must free() the returned string.
 */
wchar_t *autorun_read_combined(bool disable_autorun);

#endif /* PLUMBUS_AUTORUN_H */
