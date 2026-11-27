#ifndef PLUMBUS_BACKEND_H
#define PLUMBUS_BACKEND_H

#include <stdbool.h>
#include <stddef.h>
#include <wchar.h>

typedef enum {
    BACKEND_OK = 0,
    BACKEND_ERR_NOT_FOUND,
    BACKEND_ERR_SELF_LOOP,
    BACKEND_ERR_IO
} backend_status_t;

/*
 * Compares two file paths by physical file identity (VolumeSerial + FileIndex).
 * Falls back to normalized full-path case-insensitive comparison if handles fail.
 */
bool backend_is_same_file(const wchar_t *path_a, const wchar_t *path_b);

/*
 * Resolves the backend cmd.exe path in order:
 *  1. PLUMBUS_CMD_BACKEND env override
 *  2. PLUMBUS_DEV_BACKUP_PATH (when dev backup mode enabled and file exists)
 *  3. GetSystemDirectoryW() + "\cmd.exe"
 * Verifies the resolved path is NOT the currently running executable (anti-recursion).
 */
backend_status_t backend_resolve_cmd(wchar_t *out_path, size_t out_cap);

#endif /* PLUMBUS_BACKEND_H */
