#ifndef PLUMBUS_GIT_NATIVE_H
#define PLUMBUS_GIT_NATIVE_H

#include <stdbool.h>
#include <wchar.h>

typedef struct {
    bool in_repo;
    bool is_detached;
    bool is_dirty;
    bool has_untracked;
    wchar_t branch[256];
} git_status_t;

/*
 * Pure-C native Git status inspector (< 1 ms, NEVER invokes git.exe).
 * Walks upward from cwd for .git, parses HEAD, binary DIRC index (v2/v3/v4),
 * .gitignore, and checks working tree state via Win32 APIs.
 */
bool git_native_inspect(const wchar_t *cwd, git_status_t *out);

#endif /* PLUMBUS_GIT_NATIVE_H */
