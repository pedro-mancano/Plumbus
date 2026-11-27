#ifndef PLUMBUS_STATE_SYNC_H
#define PLUMBUS_STATE_SYNC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <wchar.h>

typedef struct {
    int32_t exit_code;
    wchar_t *cwd;
    wchar_t *env_block;      /* Double-null-terminated UTF-16 block */
    size_t env_block_wchars; /* Total wchar_t count including trailing double-null */
} synced_state_t;

/*
 * Executed by the --__sync-state helper: writes exit_code, GetCurrentDirectoryW(),
 * and GetEnvironmentStringsW() as binary UTF-16LE to file_path.
 * Returns exit_code on success, or non-zero on I/O failure.
 */
int state_sync_write_current(const wchar_t *file_path, int exit_code);

/*
 * Reads a binary state file written by state_sync_write_current and deletes file_path.
 */
bool state_sync_read_file(const wchar_t *file_path, synced_state_t *out_state);

/*
 * Applies cwd and atomic environment diff (including =X: drive vars, removing deleted vars,
 * and NEVER injecting a literal ERRORLEVEL variable).
 */
bool state_sync_apply(const synced_state_t *state);

void state_sync_free(synced_state_t *state);

#endif /* PLUMBUS_STATE_SYNC_H */
