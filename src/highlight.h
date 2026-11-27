#ifndef PLUMBUS_HIGHLIGHT_H
#define PLUMBUS_HIGHLIGHT_H

#include <stdbool.h>
#include <stddef.h>
#include <wchar.h>

typedef bool (*highlight_alias_lookup_fn)(const wchar_t *name, void *user_data);

/*
 * Renders line with ANSI 24-bit syntax highlighting + optional dark-gray ghost suffix.
 * Caller must free() the returned heap-allocated string.
 */
wchar_t *highlight_render_line(const wchar_t *line,
                               const wchar_t *ghost_suffix,
                               highlight_alias_lookup_fn alias_fn,
                               void *alias_user_data);

#endif /* PLUMBUS_HIGHLIGHT_H */
