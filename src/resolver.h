#ifndef PLUMBUS_RESOLVER_H
#define PLUMBUS_RESOLVER_H

#include <stdbool.h>
#include <stddef.h>
#include <wchar.h>

typedef enum {
    CMD_KIND_EMPTY = 0,
    CMD_KIND_NATIVE_FASTPATH,
    CMD_KIND_EXTERNAL_EXE,
    CMD_KIND_BATCH_SCRIPT,
    CMD_KIND_CMD_INTERNAL,
    CMD_KIND_CMD_COMPOUND
} cmd_kind_t;

typedef struct {
    cmd_kind_t kind;
    wchar_t resolved_path[32768];
    bool has_shell_meta;
} cmd_resolution_t;

bool resolver_is_cmd_internal(const wchar_t *name);
bool resolver_command_exists(const wchar_t *token);
bool resolver_classify_and_find(const wchar_t *line, cmd_resolution_t *out_res);

#endif /* PLUMBUS_RESOLVER_H */
