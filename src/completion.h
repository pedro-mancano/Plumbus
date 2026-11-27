#ifndef PLUMBUS_COMPLETION_H
#define PLUMBUS_COMPLETION_H

#include <stddef.h>
#include <wchar.h>

typedef struct {
    wchar_t **items;
    size_t count;
    size_t replace_start; /* Offset in line where current token begins */
    size_t replace_len;   /* Length of token being replaced */
} completion_list_t;

void completion_generate(const wchar_t *line, size_t cursor, completion_list_t *out);
void completion_free(completion_list_t *list);

#endif /* PLUMBUS_COMPLETION_H */
