#include "builtins.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wctype.h>

static bool push_dir_stack(shell_ctx_t *ctx, const wchar_t *dir) {
    if (ctx->dir_stack_len == ctx->dir_stack_cap) {
        size_t ncap = ctx->dir_stack_cap ? ctx->dir_stack_cap * 2 : 16;
        wchar_t **nb = (wchar_t **)realloc(ctx->dir_stack, ncap * sizeof(wchar_t *));
        if (!nb) return false;
        ctx->dir_stack = nb;
        ctx->dir_stack_cap = ncap;
    }
    wchar_t *copy = _wcsdup(dir);
    if (!copy) return false;
    ctx->dir_stack[ctx->dir_stack_len++] = copy;
    return true;
}

int builtin_cd(shell_ctx_t *ctx, const wchar_t *tail, const cmd_io_t *io) {
    (void)ctx;
    const wchar_t *args = cmd_skip_ws(tail);
    if (*args == L'\0') {
        wchar_t cwd[MAX_PATH * 4];
        if (GetCurrentDirectoryW((DWORD)(sizeof(cwd) / sizeof(cwd[0])), cwd) > 0) {
            cmd_io_printf(io->h_out, L"%ls\r\n", cwd);
        }
        return 0;
    }
    if (_wcsnicmp(args, L"/d", 2) == 0 && (args[2] == L' ' || args[2] == L'\t')) {
        args = cmd_skip_ws(args + 2);
    }
    wchar_t target[MAX_PATH * 4];
    wcsncpy(target, args, (sizeof(target) / sizeof(target[0])) - 1);
    target[(sizeof(target) / sizeof(target[0])) - 1] = L'\0';
    cmd_trim_trailing_ws(target);
    cmd_strip_outer_quotes(target);

    if (!SetCurrentDirectoryW(target)) {
        cmd_io_write_sz(io->h_err, L"The system cannot find the path specified.\r\n");
        return 1;
    }
    wchar_t new_cwd[MAX_PATH * 4];
    if (GetCurrentDirectoryW((DWORD)(sizeof(new_cwd) / sizeof(new_cwd[0])), new_cwd) > 0) {
        cmd_update_drive_cwd_env(new_cwd);
    }
    return 0;
}

int builtin_pushd(shell_ctx_t *ctx, const wchar_t *tail, const cmd_io_t *io) {
    wchar_t old_cwd[MAX_PATH * 4];
    if (GetCurrentDirectoryW((DWORD)(sizeof(old_cwd) / sizeof(old_cwd[0])), old_cwd) == 0) {
        return 1;
    }
    const wchar_t *args = cmd_skip_ws(tail);
    if (*args == L'\0') {
        for (size_t i = ctx->dir_stack_len; i > 0; i--) {
            cmd_io_printf(io->h_out, L"%ls\r\n", ctx->dir_stack[i - 1]);
        }
        return 0;
    }
    wchar_t target[MAX_PATH * 4];
    wcsncpy(target, args, (sizeof(target) / sizeof(target[0])) - 1);
    target[(sizeof(target) / sizeof(target[0])) - 1] = L'\0';
    cmd_trim_trailing_ws(target);
    cmd_strip_outer_quotes(target);
    if (!SetCurrentDirectoryW(target)) {
        cmd_io_write_sz(io->h_err, L"The system cannot find the path specified.\r\n");
        return 1;
    }
    push_dir_stack(ctx, old_cwd);
    wchar_t new_cwd[MAX_PATH * 4];
    if (GetCurrentDirectoryW((DWORD)(sizeof(new_cwd) / sizeof(new_cwd[0])), new_cwd) > 0) {
        cmd_update_drive_cwd_env(new_cwd);
    }
    return 0;
}

int builtin_popd(shell_ctx_t *ctx, const wchar_t *tail, const cmd_io_t *io) {
    (void)tail;
    (void)io;
    if (!ctx || ctx->dir_stack_len == 0) return 0;
    wchar_t *prev = ctx->dir_stack[--ctx->dir_stack_len];
    BOOL ok = SetCurrentDirectoryW(prev);
    if (ok) cmd_update_drive_cwd_env(prev);
    free(prev);
    return ok ? 0 : 1;
}

static bool mkdir_recursive(const wchar_t *path) {
    wchar_t tmp[MAX_PATH * 4];
    wcsncpy(tmp, path, (sizeof(tmp) / sizeof(tmp[0])) - 1);
    tmp[(sizeof(tmp) / sizeof(tmp[0])) - 1] = L'\0';
    size_t len = wcslen(tmp);
    if (len == 0) return false;
    while (len > 1 && (tmp[len - 1] == L'\\' || tmp[len - 1] == L'/')) tmp[--len] = L'\0';

    for (wchar_t *p = tmp; *p; p++) {
        if (*p == L'\\' || *p == L'/') {
            if (p == tmp || (p == tmp + 2 && tmp[1] == L':')) continue;
            wchar_t save = *p;
            *p = L'\0';
            DWORD attr = GetFileAttributesW(tmp);
            if (attr == INVALID_FILE_ATTRIBUTES) {
                if (!CreateDirectoryW(tmp, NULL) && GetLastError() != ERROR_ALREADY_EXISTS) {
                    *p = save;
                    return false;
                }
            }
            *p = save;
        }
    }
    DWORD attr = GetFileAttributesW(tmp);
    if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY)) return true;
    return CreateDirectoryW(tmp, NULL) != 0 || GetLastError() == ERROR_ALREADY_EXISTS;
}

int builtin_mkdir(shell_ctx_t *ctx, const wchar_t *tail, const cmd_io_t *io) {
    (void)ctx;
    int argc = 0;
    wchar_t **argv = NULL;
    if (!cmd_parse_args(tail, &argc, &argv) || argc == 0) {
        cmd_free_args(argc, argv);
        cmd_io_write_sz(io->h_err, L"The syntax of the command is incorrect.\r\n");
        return 1;
    }
    int rc = 0;
    for (int i = 0; i < argc; i++) {
        if (!mkdir_recursive(argv[i])) {
            cmd_io_printf(io->h_err, L"A subdirectory or file %ls already exists or cannot be created.\r\n", argv[i]);
            rc = 1;
        }
    }
    cmd_free_args(argc, argv);
    return rc;
}

static bool rmdir_recursive(const wchar_t *path) {
    wchar_t pat[MAX_PATH * 4];
    swprintf(pat, sizeof(pat) / sizeof(pat[0]), L"%ls\\*", path);
    WIN32_FIND_DATAW fd;
    HANDLE hf = FindFirstFileW(pat, &fd);
    if (hf != INVALID_HANDLE_VALUE) {
        do {
            if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) continue;
            wchar_t child[MAX_PATH * 4];
            swprintf(child, sizeof(child) / sizeof(child[0]), L"%ls\\%ls", path, fd.cFileName);
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_READONLY) {
                SetFileAttributesW(child, fd.dwFileAttributes & ~FILE_ATTRIBUTE_READONLY);
            }
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                if (!rmdir_recursive(child)) { FindClose(hf); return false; }
            } else {
                if (!DeleteFileW(child)) { FindClose(hf); return false; }
            }
        } while (FindNextFileW(hf, &fd));
        FindClose(hf);
    }
    DWORD a = GetFileAttributesW(path);
    if (a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_READONLY)) {
        SetFileAttributesW(path, a & ~FILE_ATTRIBUTE_READONLY);
    }
    return RemoveDirectoryW(path) != 0;
}

int builtin_rmdir(shell_ctx_t *ctx, const wchar_t *tail, const cmd_io_t *io) {
    (void)ctx;
    int argc = 0;
    wchar_t **argv = NULL;
    if (!cmd_parse_args(tail, &argc, &argv) || argc == 0) {
        cmd_free_args(argc, argv);
        cmd_io_write_sz(io->h_err, L"The syntax of the command is incorrect.\r\n");
        return 1;
    }
    bool recursive = false;
    bool quiet = false;
    for (int i = 0; i < argc; i++) {
        if (_wcsicmp(argv[i], L"/s") == 0) recursive = true;
        else if (_wcsicmp(argv[i], L"/q") == 0) quiet = true;
    }
    (void)quiet;
    int rc = 0;
    for (int i = 0; i < argc; i++) {
        if (argv[i][0] == L'/') continue;
        bool ok = recursive ? rmdir_recursive(argv[i]) : (RemoveDirectoryW(argv[i]) != 0);
        if (!ok) {
            cmd_io_printf(io->h_err, L"The system cannot remove directory %ls.\r\n", argv[i]);
            rc = 1;
        }
    }
    cmd_free_args(argc, argv);
    return rc;
}

static void split_dir_part(const wchar_t *spec, wchar_t *dir_out, size_t cap) {
    const wchar_t *sl = wcsrchr(spec, L'\\');
    const wchar_t *fsl = wcsrchr(spec, L'/');
    if (fsl && (!sl || fsl > sl)) sl = fsl;
    if (!sl) { dir_out[0] = L'\0'; return; }
    size_t n = (size_t)(sl - spec) + 1;
    if (n >= cap) n = cap - 1;
    memcpy(dir_out, spec, n * sizeof(wchar_t));
    dir_out[n] = L'\0';
}

static int del_pattern(const wchar_t *pat, bool force, bool recursive, const cmd_io_t *io) {
    wchar_t dir[MAX_PATH * 4];
    split_dir_part(pat, dir, sizeof(dir) / sizeof(dir[0]));

    int deleted = 0;
    WIN32_FIND_DATAW fd;
    HANDLE hf = FindFirstFileW(pat, &fd);
    if (hf != INVALID_HANDLE_VALUE) {
        do {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
            wchar_t full[MAX_PATH * 4];
            swprintf(full, sizeof(full) / sizeof(full[0]), L"%ls%ls", dir, fd.cFileName);
            if (force && (fd.dwFileAttributes & FILE_ATTRIBUTE_READONLY)) {
                SetFileAttributesW(full, fd.dwFileAttributes & ~FILE_ATTRIBUTE_READONLY);
            }
            if (DeleteFileW(full)) deleted++;
        } while (FindNextFileW(hf, &fd));
        FindClose(hf);
    }

    if (recursive) {
        const wchar_t *name_part = pat + wcslen(dir);
        wchar_t sub_pat[MAX_PATH * 4];
        swprintf(sub_pat, sizeof(sub_pat) / sizeof(sub_pat[0]), L"%ls*", dir);
        HANDLE hs = FindFirstFileW(sub_pat, &fd);
        if (hs != INVALID_HANDLE_VALUE) {
            do {
                if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
                if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) continue;
                wchar_t next_pat[MAX_PATH * 4];
                swprintf(next_pat, sizeof(next_pat) / sizeof(next_pat[0]), L"%ls%ls\\%ls", dir, fd.cFileName, name_part);
                del_pattern(next_pat, force, true, io);
            } while (FindNextFileW(hs, &fd));
            FindClose(hs);
        }
    }

    if (deleted == 0 && !recursive) {
        cmd_io_printf(io->h_err, L"Could Not Find %ls\r\n", pat);
        return 1;
    }
    return 0;
}

int builtin_del(shell_ctx_t *ctx, const wchar_t *tail, const cmd_io_t *io) {
    (void)ctx;
    int argc = 0;
    wchar_t **argv = NULL;
    if (!cmd_parse_args(tail, &argc, &argv) || argc == 0) {
        cmd_free_args(argc, argv);
        cmd_io_write_sz(io->h_err, L"The syntax of the command is incorrect.\r\n");
        return 1;
    }
    bool force = false, recursive = false;
    for (int i = 0; i < argc; i++) {
        if (_wcsicmp(argv[i], L"/f") == 0) force = true;
        else if (_wcsicmp(argv[i], L"/s") == 0) recursive = true;
    }
    int rc = 0;
    for (int i = 0; i < argc; i++) {
        if (argv[i][0] == L'/') continue;
        DWORD attr = GetFileAttributesW(argv[i]);
        if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY)) {
            wchar_t pat[MAX_PATH * 4];
            swprintf(pat, sizeof(pat) / sizeof(pat[0]), L"%ls\\*", argv[i]);
            if (del_pattern(pat, force, recursive, io) != 0) rc = 1;
        } else {
            if (del_pattern(argv[i], force, recursive, io) != 0) rc = 1;
        }
    }
    cmd_free_args(argc, argv);
    return rc;
}

int builtin_type(shell_ctx_t *ctx, const wchar_t *tail, const cmd_io_t *io) {
    (void)ctx;
    int argc = 0;
    wchar_t **argv = NULL;
    if (!cmd_parse_args(tail, &argc, &argv) || argc == 0) {
        cmd_free_args(argc, argv);
        cmd_io_write_sz(io->h_err, L"The syntax of the command is incorrect.\r\n");
        return 1;
    }
    int rc = 0;
    for (int i = 0; i < argc; i++) {
        wchar_t dir[MAX_PATH * 4];
        split_dir_part(argv[i], dir, sizeof(dir) / sizeof(dir[0]));
        WIN32_FIND_DATAW fd;
        HANDLE hf = FindFirstFileW(argv[i], &fd);
        if (hf == INVALID_HANDLE_VALUE) {
            cmd_io_write_sz(io->h_err, L"The system cannot find the file specified.\r\n");
            rc = 1;
            continue;
        }
        bool any = false;
        do {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
            any = true;
            wchar_t full[MAX_PATH * 4];
            swprintf(full, sizeof(full) / sizeof(full[0]), L"%ls%ls", dir, fd.cFileName);
            HANDLE f = CreateFileW(full, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                   NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
            if (f == INVALID_HANDLE_VALUE) {
                rc = 1;
                continue;
            }
            char buf[8192];
            DWORD rd = 0;
            while (ReadFile(f, buf, sizeof(buf), &rd, NULL) && rd > 0) {
                DWORD mode = 0;
                if (GetConsoleMode(io->h_out, &mode)) {
                    wchar_t wbuf[8192];
                    int wn = MultiByteToWideChar(CP_UTF8, 0, buf, (int)rd, wbuf, 8192);
                    if (wn <= 0) wn = MultiByteToWideChar(CP_ACP, 0, buf, (int)rd, wbuf, 8192);
                    if (wn > 0) cmd_io_write_w(io->h_out, wbuf, (size_t)wn);
                } else {
                    DWORD wr = 0;
                    WriteFile(io->h_out, buf, rd, &wr, NULL);
                }
            }
            CloseHandle(f);
        } while (FindNextFileW(hf, &fd));
        FindClose(hf);
        if (!any) rc = 1;
    }
    cmd_free_args(argc, argv);
    return rc;
}

static bool copy_concat(const wchar_t *src_list, const wchar_t *dst_path, bool binary_mode) {
    HANDLE out = CreateFileW(dst_path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (out == INVALID_HANDLE_VALUE) return false;
    wchar_t *dup = _wcsdup(src_list);
    if (!dup) { CloseHandle(out); return false; }

    bool ok = true;
    wchar_t *ctx_tok = NULL;
    for (wchar_t *part = wcstok_s(dup, L"+", &ctx_tok); part; part = wcstok_s(NULL, L"+", &ctx_tok)) {
        HANDLE in = CreateFileW(part, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (in == INVALID_HANDLE_VALUE) { ok = false; break; }
        char buf[16384];
        DWORD rd = 0, wr = 0;
        while (ReadFile(in, buf, sizeof(buf), &rd, NULL) && rd > 0) {
            if (!WriteFile(out, buf, rd, &wr, NULL)) { ok = false; break; }
        }
        CloseHandle(in);
        if (!ok) break;
    }
    if (ok && !binary_mode) {
        char eof_sub = 0x1A;
        DWORD wr = 0;
        WriteFile(out, &eof_sub, 1, &wr, NULL);
    }
    free(dup);
    CloseHandle(out);
    return ok;
}

int builtin_copy(shell_ctx_t *ctx, const wchar_t *tail, const cmd_io_t *io) {
    (void)ctx;
    int argc = 0;
    wchar_t **argv = NULL;
    if (!cmd_parse_args(tail, &argc, &argv)) return 1;

    const wchar_t *pos_args[4] = { NULL, NULL, NULL, NULL };
    int pos_n = 0;
    bool fail_if_exists = false;
    bool binary_mode = false;
    for (int i = 0; i < argc; i++) {
        if (_wcsicmp(argv[i], L"/y") == 0) fail_if_exists = false;
        else if (_wcsicmp(argv[i], L"/-y") == 0) fail_if_exists = true;
        else if (_wcsicmp(argv[i], L"/b") == 0) binary_mode = true;
        else if (_wcsicmp(argv[i], L"/a") == 0) binary_mode = false;
        else if (argv[i][0] == L'/') continue;
        else if (pos_n < 4) pos_args[pos_n++] = argv[i];
    }

    if (pos_n < 1) {
        cmd_free_args(argc, argv);
        cmd_io_write_sz(io->h_err, L"The syntax of the command is incorrect.\r\n");
        return 1;
    }

    const wchar_t *src = pos_args[0];
    const wchar_t *dst = (pos_n >= 2) ? pos_args[1] : L".";

    if (wcschr(src, L'+') && pos_n >= 2) {
        bool ok = copy_concat(src, dst, binary_mode);
        cmd_free_args(argc, argv);
        if (ok) {
            cmd_io_write_sz(io->h_out, L"        1 file(s) copied.\r\n");
            return 0;
        }
        cmd_io_write_sz(io->h_err, L"The system cannot find the file specified.\r\n");
        return 1;
    }

    DWORD dst_attr = GetFileAttributesW(dst);
    bool dst_is_dir = (dst_attr != INVALID_FILE_ATTRIBUTES && (dst_attr & FILE_ATTRIBUTE_DIRECTORY));

    wchar_t src_dir[MAX_PATH * 4];
    split_dir_part(src, src_dir, sizeof(src_dir) / sizeof(src_dir[0]));
    WIN32_FIND_DATAW fd;
    HANDLE hf = FindFirstFileW(src, &fd);
    if (hf == INVALID_HANDLE_VALUE) {
        cmd_free_args(argc, argv);
        cmd_io_write_sz(io->h_err, L"The system cannot find the file specified.\r\n");
        return 1;
    }

    int copied = 0;
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        wchar_t sfull[MAX_PATH * 4], dfull[MAX_PATH * 4];
        swprintf(sfull, sizeof(sfull) / sizeof(sfull[0]), L"%ls%ls", src_dir, fd.cFileName);
        if (dst_is_dir) {
            swprintf(dfull, sizeof(dfull) / sizeof(dfull[0]), L"%ls\\%ls", dst, fd.cFileName);
        } else {
            wcsncpy(dfull, dst, (sizeof(dfull) / sizeof(dfull[0])) - 1);
            dfull[(sizeof(dfull) / sizeof(dfull[0])) - 1] = L'\0';
        }
        if (CopyFileW(sfull, dfull, fail_if_exists ? TRUE : FALSE)) copied++;
    } while (FindNextFileW(hf, &fd));
    FindClose(hf);
    cmd_free_args(argc, argv);

    cmd_io_printf(io->h_out, L"        %d file(s) copied.\r\n", copied);
    return (copied > 0) ? 0 : 1;
}

int builtin_move(shell_ctx_t *ctx, const wchar_t *tail, const cmd_io_t *io) {
    (void)ctx;
    int argc = 0;
    wchar_t **argv = NULL;
    if (!cmd_parse_args(tail, &argc, &argv)) return 1;

    const wchar_t *src = NULL, *dst = NULL;
    DWORD flags = MOVEFILE_COPY_ALLOWED | MOVEFILE_REPLACE_EXISTING;
    for (int i = 0; i < argc; i++) {
        if (_wcsicmp(argv[i], L"/y") == 0) flags |= MOVEFILE_REPLACE_EXISTING;
        else if (_wcsicmp(argv[i], L"/-y") == 0) flags &= ~MOVEFILE_REPLACE_EXISTING;
        else if (argv[i][0] == L'/') continue;
        else if (!src) src = argv[i];
        else if (!dst) dst = argv[i];
    }

    if (!src || !dst) {
        cmd_free_args(argc, argv);
        cmd_io_write_sz(io->h_err, L"The syntax of the command is incorrect.\r\n");
        return 1;
    }

    DWORD dst_attr = GetFileAttributesW(dst);
    bool dst_is_dir = (dst_attr != INVALID_FILE_ATTRIBUTES && (dst_attr & FILE_ATTRIBUTE_DIRECTORY));
    wchar_t src_dir[MAX_PATH * 4];
    split_dir_part(src, src_dir, sizeof(src_dir) / sizeof(src_dir[0]));

    WIN32_FIND_DATAW fd;
    HANDLE hf = FindFirstFileW(src, &fd);
    if (hf == INVALID_HANDLE_VALUE) {
        cmd_free_args(argc, argv);
        cmd_io_write_sz(io->h_err, L"The system cannot find the file specified.\r\n");
        return 1;
    }

    int moved = 0;
    do {
        if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) continue;
        wchar_t sfull[MAX_PATH * 4], dfull[MAX_PATH * 4];
        swprintf(sfull, sizeof(sfull) / sizeof(sfull[0]), L"%ls%ls", src_dir, fd.cFileName);
        if (dst_is_dir) {
            swprintf(dfull, sizeof(dfull) / sizeof(dfull[0]), L"%ls\\%ls", dst, fd.cFileName);
        } else {
            wcsncpy(dfull, dst, (sizeof(dfull) / sizeof(dfull[0])) - 1);
            dfull[(sizeof(dfull) / sizeof(dfull[0])) - 1] = L'\0';
        }
        if (MoveFileExW(sfull, dfull, flags)) moved++;
    } while (FindNextFileW(hf, &fd));
    FindClose(hf);
    cmd_free_args(argc, argv);

    cmd_io_printf(io->h_out, L"        %d file(s) moved.\r\n", moved);
    return (moved > 0) ? 0 : 1;
}

static void apply_ren_wildcard(const wchar_t *src_name, const wchar_t *dst_mask, wchar_t *out, size_t cap) {
    if (!wcschr(dst_mask, L'*') && !wcschr(dst_mask, L'?')) {
        wcsncpy(out, dst_mask, cap - 1);
        out[cap - 1] = L'\0';
        return;
    }
    const wchar_t *sdot = wcsrchr(src_name, L'.');
    const wchar_t *ddot = wcsrchr(dst_mask, L'.');

    wchar_t s_stem[MAX_PATH] = { 0 }, s_ext[MAX_PATH] = { 0 };
    if (sdot) {
        size_t n = (size_t)(sdot - src_name);
        if (n >= MAX_PATH) n = MAX_PATH - 1;
        memcpy(s_stem, src_name, n * sizeof(wchar_t));
        wcsncpy(s_ext, sdot + 1, MAX_PATH - 1);
    } else {
        wcsncpy(s_stem, src_name, MAX_PATH - 1);
    }

    wchar_t d_stem[MAX_PATH] = { 0 }, d_ext[MAX_PATH] = { 0 };
    if (ddot) {
        size_t n = (size_t)(ddot - dst_mask);
        if (n >= MAX_PATH) n = MAX_PATH - 1;
        memcpy(d_stem, dst_mask, n * sizeof(wchar_t));
        wcsncpy(d_ext, ddot + 1, MAX_PATH - 1);
    } else {
        wcsncpy(d_stem, dst_mask, MAX_PATH - 1);
    }

    size_t oi = 0;
    for (size_t i = 0; d_stem[i] && oi + 1 < cap; i++) {
        if (d_stem[i] == L'*') {
            for (size_t j = 0; s_stem[j] && oi + 1 < cap; j++) out[oi++] = s_stem[j];
            break;
        } else if (d_stem[i] == L'?') {
            if (i < wcslen(s_stem)) out[oi++] = s_stem[i];
        } else {
            out[oi++] = d_stem[i];
        }
    }
    if (ddot && oi + 1 < cap) {
        out[oi++] = L'.';
        for (size_t i = 0; d_ext[i] && oi + 1 < cap; i++) {
            if (d_ext[i] == L'*') {
                for (size_t j = 0; s_ext[j] && oi + 1 < cap; j++) out[oi++] = s_ext[j];
                break;
            } else if (d_ext[i] == L'?') {
                if (i < wcslen(s_ext)) out[oi++] = s_ext[i];
            } else {
                out[oi++] = d_ext[i];
            }
        }
    }
    out[oi] = L'\0';
}

int builtin_ren(shell_ctx_t *ctx, const wchar_t *tail, const cmd_io_t *io) {
    (void)ctx;
    int argc = 0;
    wchar_t **argv = NULL;
    if (!cmd_parse_args(tail, &argc, &argv) || argc < 2) {
        cmd_free_args(argc, argv);
        cmd_io_write_sz(io->h_err, L"The syntax of the command is incorrect.\r\n");
        return 1;
    }
    wchar_t src_dir[MAX_PATH * 4];
    split_dir_part(argv[0], src_dir, sizeof(src_dir) / sizeof(src_dir[0]));

    if (wcschr(argv[0], L'*') || wcschr(argv[0], L'?')) {
        WIN32_FIND_DATAW fd;
        HANDLE hf = FindFirstFileW(argv[0], &fd);
        if (hf == INVALID_HANDLE_VALUE) {
            cmd_free_args(argc, argv);
            cmd_io_write_sz(io->h_err, L"The system cannot find the file specified.\r\n");
            return 1;
        }
        int ok_cnt = 0;
        do {
            if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) continue;
            wchar_t new_name[MAX_PATH];
            apply_ren_wildcard(fd.cFileName, argv[1], new_name, MAX_PATH);
            wchar_t sfull[MAX_PATH * 4], dfull[MAX_PATH * 4];
            swprintf(sfull, sizeof(sfull) / sizeof(sfull[0]), L"%ls%ls", src_dir, fd.cFileName);
            swprintf(dfull, sizeof(dfull) / sizeof(dfull[0]), L"%ls%ls", src_dir, new_name);
            if (MoveFileW(sfull, dfull)) ok_cnt++;
        } while (FindNextFileW(hf, &fd));
        FindClose(hf);
        cmd_free_args(argc, argv);
        return (ok_cnt > 0) ? 0 : 1;
    }

    wchar_t dfull[MAX_PATH * 4];
    swprintf(dfull, sizeof(dfull) / sizeof(dfull[0]), L"%ls%ls", src_dir, argv[1]);
    BOOL ok = MoveFileW(argv[0], dfull);
    cmd_free_args(argc, argv);
    if (!ok) {
        cmd_io_write_sz(io->h_err, L"The system cannot find the file specified.\r\n");
        return 1;
    }
    return 0;
}

int builtin_mklink(shell_ctx_t *ctx, const wchar_t *tail, const cmd_io_t *io) {
    (void)ctx;
    int argc = 0;
    wchar_t **argv = NULL;
    if (!cmd_parse_args(tail, &argc, &argv)) return 1;

    bool dir = false, hard = false, junc = false;
    const wchar_t *link = NULL, *target = NULL;
    for (int i = 0; i < argc; i++) {
        if (_wcsicmp(argv[i], L"/d") == 0) dir = true;
        else if (_wcsicmp(argv[i], L"/h") == 0) hard = true;
        else if (_wcsicmp(argv[i], L"/j") == 0) junc = true;
        else if (!link) link = argv[i];
        else if (!target) target = argv[i];
    }
    if (!link || !target) {
        cmd_free_args(argc, argv);
        cmd_io_write_sz(io->h_err, L"The syntax of the command is incorrect.\r\n");
        return 1;
    }
    BOOL ok = FALSE;
    if (hard) {
        ok = CreateHardLinkW(link, target, NULL);
    } else {
        DWORD flags = (dir || junc) ? SYMBOLIC_LINK_FLAG_DIRECTORY : 0;
        flags |= 0x2; /* SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE */
        ok = CreateSymbolicLinkW(link, target, flags);
    }
    if (ok) {
        cmd_io_printf(io->h_out, L"symbolic link created for %ls <<===>> %ls\r\n", link, target);
    } else {
        cmd_io_write_sz(io->h_err, L"You do not have sufficient privilege to perform this operation.\r\n");
    }
    cmd_free_args(argc, argv);
    return ok ? 0 : 1;
}

/* --- DIR implementation --- */
typedef struct {
    WIN32_FIND_DATAW fd;
} dir_item_t;

typedef struct {
    bool bare;
    bool wide;
    bool recurse;
    bool filter_attr;
    DWORD req_attr;
    DWORD exc_attr;
    wchar_t sort_key; /* n, e, s, d */
    bool sort_desc;
} dir_opts_t;

static int cmp_dir_item(const void *a, const void *b, void *ctx_ptr) {
    const dir_item_t *ia = (const dir_item_t *)a;
    const dir_item_t *ib = (const dir_item_t *)b;
    const dir_opts_t *opts = (const dir_opts_t *)ctx_ptr;
    int r = 0;
    switch (opts->sort_key) {
        case L's': {
            ULONGLONG sa = (((ULONGLONG)ia->fd.nFileSizeHigh) << 32) | ia->fd.nFileSizeLow;
            ULONGLONG sb = (((ULONGLONG)ib->fd.nFileSizeHigh) << 32) | ib->fd.nFileSizeLow;
            r = (sa < sb) ? -1 : (sa > sb ? 1 : 0);
            break;
        }
        case L'd':
            r = CompareFileTime(&ia->fd.ftLastWriteTime, &ib->fd.ftLastWriteTime);
            break;
        case L'e': {
            const wchar_t *ea = wcsrchr(ia->fd.cFileName, L'.');
            const wchar_t *eb = wcsrchr(ib->fd.cFileName, L'.');
            r = _wcsicmp(ea ? ea : L"", eb ? eb : L"");
            if (r == 0) r = _wcsicmp(ia->fd.cFileName, ib->fd.cFileName);
            break;
        }
        default:
            r = _wcsicmp(ia->fd.cFileName, ib->fd.cFileName);
            break;
    }
    return opts->sort_desc ? -r : r;
}

/* Wrapper for qsort (since qsort_s has different param order on Windows vs POSIX) */
static const dir_opts_t *g_sort_opts = NULL;
static int cmp_dir_item_qsort(const void *a, const void *b) {
    return cmp_dir_item(a, b, (void *)(size_t)g_sort_opts);
}

static void dir_walk(const wchar_t *dir_path, const wchar_t *mask, const dir_opts_t *opts,
                     const cmd_io_t *io, ULONGLONG *tot_files, ULONGLONG *tot_dirs, ULONGLONG *tot_bytes) {
    wchar_t pat[MAX_PATH * 4];
    swprintf(pat, sizeof(pat) / sizeof(pat[0]), L"%ls\\%ls", dir_path, mask);

    size_t cap = 64, count = 0;
    dir_item_t *items = (dir_item_t *)malloc(cap * sizeof(dir_item_t));
    if (!items) return;

    WIN32_FIND_DATAW fd;
    HANDLE hf = FindFirstFileW(pat, &fd);
    if (hf != INVALID_HANDLE_VALUE) {
        do {
            if (opts->bare && (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0)) continue;
            if (opts->filter_attr) {
                if ((fd.dwFileAttributes & opts->req_attr) != opts->req_attr) continue;
                if ((fd.dwFileAttributes & opts->exc_attr) != 0) continue;
            }
            if (count == cap) {
                cap *= 2;
                dir_item_t *ni = (dir_item_t *)realloc(items, cap * sizeof(dir_item_t));
                if (!ni) break;
                items = ni;
            }
            items[count++].fd = fd;
        } while (FindNextFileW(hf, &fd));
        FindClose(hf);
    }

    if (count > 1 && opts->sort_key) {
        g_sort_opts = opts;
        qsort(items, count, sizeof(dir_item_t), cmp_dir_item_qsort);
    }

    if (!opts->bare && count > 0) {
        cmd_io_printf(io->h_out, L"\r\n Directory of %ls\r\n\r\n", dir_path);
    }

    ULONGLONG dir_files = 0, dir_dirs = 0, dir_bytes = 0;
    for (size_t i = 0; i < count; i++) {
        const WIN32_FIND_DATAW *f = &items[i].fd;
        bool is_dir = (f->dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        ULONGLONG sz = (((ULONGLONG)f->nFileSizeHigh) << 32) | f->nFileSizeLow;
        if (is_dir) dir_dirs++;
        else { dir_files++; dir_bytes += sz; }

        if (opts->bare) {
            if (opts->recurse) {
                cmd_io_printf(io->h_out, L"%ls\\%ls\r\n", dir_path, f->cFileName);
            } else {
                cmd_io_printf(io->h_out, L"%ls\r\n", f->cFileName);
            }
        } else if (opts->wide) {
            if (is_dir) cmd_io_printf(io->h_out, L"[%ls]  ", f->cFileName);
            else cmd_io_printf(io->h_out, L"%-20ls  ", f->cFileName);
            if ((i + 1) % 4 == 0) cmd_io_write_sz(io->h_out, L"\r\n");
        } else {
            FILETIME lft;
            SYSTEMTIME st;
            FileTimeToLocalFileTime(&f->ftLastWriteTime, &lft);
            FileTimeToSystemTime(&lft, &st);
            if (is_dir) {
                cmd_io_printf(io->h_out, L"%02u/%02u/%04u  %02u:%02u    <DIR>          %ls\r\n",
                              (unsigned)st.wMonth, (unsigned)st.wDay, (unsigned)st.wYear,
                              (unsigned)st.wHour, (unsigned)st.wMinute, f->cFileName);
            } else {
                cmd_io_printf(io->h_out, L"%02u/%02u/%04u  %02u:%02u    %14llu %ls\r\n",
                              (unsigned)st.wMonth, (unsigned)st.wDay, (unsigned)st.wYear,
                              (unsigned)st.wHour, (unsigned)st.wMinute,
                              (unsigned long long)sz, f->cFileName);
            }
        }
    }
    if (!opts->bare && opts->wide && (count % 4) != 0) cmd_io_write_sz(io->h_out, L"\r\n");
    if (!opts->bare && count > 0) {
        cmd_io_printf(io->h_out, L"%16llu File(s) %14llu bytes\r\n",
                      (unsigned long long)dir_files, (unsigned long long)dir_bytes);
    }

    *tot_files += dir_files;
    *tot_dirs += dir_dirs;
    *tot_bytes += dir_bytes;
    free(items);

    if (opts->recurse) {
        wchar_t sub_pat[MAX_PATH * 4];
        swprintf(sub_pat, sizeof(sub_pat) / sizeof(sub_pat[0]), L"%ls\\*", dir_path);
        HANDLE hs = FindFirstFileW(sub_pat, &fd);
        if (hs != INVALID_HANDLE_VALUE) {
            do {
                if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
                if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) continue;
                wchar_t sub[MAX_PATH * 4];
                swprintf(sub, sizeof(sub) / sizeof(sub[0]), L"%ls\\%ls", dir_path, fd.cFileName);
                dir_walk(sub, mask, opts, io, tot_files, tot_dirs, tot_bytes);
            } while (FindNextFileW(hs, &fd));
            FindClose(hs);
        }
    }
}

int builtin_dir(shell_ctx_t *ctx, const wchar_t *tail, const cmd_io_t *io) {
    (void)ctx;
    int argc = 0;
    wchar_t **argv = NULL;
    if (!cmd_parse_args(tail, &argc, &argv)) return 1;

    dir_opts_t opts = { 0 };
    const wchar_t *target = NULL;
    for (int i = 0; i < argc; i++) {
        if (_wcsicmp(argv[i], L"/b") == 0) opts.bare = true;
        else if (_wcsicmp(argv[i], L"/w") == 0) opts.wide = true;
        else if (_wcsicmp(argv[i], L"/s") == 0) opts.recurse = true;
        else if (_wcsnicmp(argv[i], L"/a", 2) == 0) {
            opts.filter_attr = true;
            const wchar_t *p = argv[i] + 2;
            if (*p == L':') p++;
            bool neg = false;
            for (; *p; p++) {
                if (*p == L'-') { neg = true; continue; }
                DWORD bit = 0;
                switch (towlower(*p)) {
                    case L'd': bit = FILE_ATTRIBUTE_DIRECTORY; break;
                    case L'h': bit = FILE_ATTRIBUTE_HIDDEN; break;
                    case L's': bit = FILE_ATTRIBUTE_SYSTEM; break;
                    case L'r': bit = FILE_ATTRIBUTE_READONLY; break;
                    case L'a': bit = FILE_ATTRIBUTE_ARCHIVE; break;
                    default: break;
                }
                if (neg) opts.exc_attr |= bit;
                else opts.req_attr |= bit;
                neg = false;
            }
        } else if (_wcsnicmp(argv[i], L"/o", 2) == 0) {
            const wchar_t *p = argv[i] + 2;
            if (*p == L':') p++;
            if (*p == L'-') { opts.sort_desc = true; p++; }
            opts.sort_key = *p ? towlower(*p) : L'n';
        } else if (argv[i][0] != L'/' && !target) {
            target = argv[i];
        }
    }

    wchar_t full_dir[MAX_PATH * 4];
    wchar_t mask[MAX_PATH] = L"*";
    if (!target) {
        if (GetCurrentDirectoryW((DWORD)(sizeof(full_dir) / sizeof(full_dir[0])), full_dir) == 0) {
            wcscpy(full_dir, L".");
        }
    } else {
        DWORD attr = GetFileAttributesW(target);
        if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY)) {
            GetFullPathNameW(target, (DWORD)(sizeof(full_dir) / sizeof(full_dir[0])), full_dir, NULL);
        } else {
            wchar_t tmp_dir[MAX_PATH * 4];
            split_dir_part(target, tmp_dir, sizeof(tmp_dir) / sizeof(tmp_dir[0]));
            if (tmp_dir[0]) {
                GetFullPathNameW(tmp_dir, (DWORD)(sizeof(full_dir) / sizeof(full_dir[0])), full_dir, NULL);
                size_t fl = wcslen(full_dir);
                if (fl > 3 && full_dir[fl - 1] == L'\\') full_dir[fl - 1] = L'\0';
                wcsncpy(mask, target + wcslen(tmp_dir), MAX_PATH - 1);
            } else {
                GetCurrentDirectoryW((DWORD)(sizeof(full_dir) / sizeof(full_dir[0])), full_dir);
                wcsncpy(mask, target, MAX_PATH - 1);
            }
            mask[MAX_PATH - 1] = L'\0';
        }
    }

    ULONGLONG tot_files = 0, tot_dirs = 0, tot_bytes = 0;
    dir_walk(full_dir, mask, &opts, io, &tot_files, &tot_dirs, &tot_bytes);

    if (!opts.bare && tot_files == 0 && tot_dirs == 0) {
        cmd_io_write_sz(io->h_err, L"File Not Found\r\n");
        cmd_free_args(argc, argv);
        return 1;
    }

    if (!opts.bare) {
        ULARGE_INTEGER free_bytes = { 0 };
        GetDiskFreeSpaceExW(full_dir, &free_bytes, NULL, NULL);
        cmd_io_printf(io->h_out, L"%16llu Dir(s)  %14llu bytes free\r\n",
                      (unsigned long long)tot_dirs, (unsigned long long)free_bytes.QuadPart);
    }

    cmd_free_args(argc, argv);
    return 0;
}
