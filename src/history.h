#ifndef PLUMBUS_HISTORY_H
#define PLUMBUS_HISTORY_H

#include <stdbool.h>
#include <stddef.h>
#include <wchar.h>

typedef struct {
    wchar_t **items;
    size_t len;
    size_t cap;
    size_t max_items;
    wchar_t file_path[32768];
} history_t;

bool history_init(history_t *h, size_t max_items);
void history_free(history_t *h);

bool history_add(history_t *h, const wchar_t *line);
const wchar_t *history_get(const history_t *h, size_t index);

/* Returns pointer inside matching history entry (suffix after prefix), or NULL */
const wchar_t *history_find_ghost_suffix(const history_t *h, const wchar_t *prefix);

/* Reverse case-insensitive substring search starting at start_idx (inclusive). Returns -1 if not found. */
ptrdiff_t history_search_reverse(const history_t *h, const wchar_t *query, ptrdiff_t start_idx);

#endif /* PLUMBUS_HISTORY_H */
