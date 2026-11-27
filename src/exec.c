#include "exec.h"
#include "resolver.h"
#include "state_sync.h"
#include "config.h"
#include "tui_config.h"
#include "console.h"
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wctype.h>

bool shell_ctx_init(shell_ctx_t *ctx, const cmd_options_t *opts, const wchar_t *backend_path) {
    if (!ctx) return false;
    memset(ctx, 0, sizeof(*ctx));
    if (backend_path) {
        wcsncpy(ctx->backend_path, backend_path, (sizeof(ctx->backend_path) / sizeof(ctx->backend_path[0])) - 1);
    }
    DWORD slen = GetModuleFileNameW(NULL, ctx->self_path, (DWORD)(sizeof(ctx->self_path) / sizeof(ctx->self_path[0])));
    if (slen == 0) return false;
    if (opts) {
        ctx->echo_off = opts->echo_off;
    }
    return true;
}

void shell_ctx_free(shell_ctx_t *ctx) {
    if (!ctx) return;
    for (size_t i = 0; i < ctx->dir_stack_len; i++) {
        free(ctx->dir_stack[i]);
    }
    free(ctx->dir_stack);
    ctx->dir_stack = NULL;
    ctx->dir_stack_len = 0;
    ctx->dir_stack_cap = 0;
}

static void update_drive_cwd_env(const wchar_t *cwd) {
    if (cwd && iswalpha(cwd[0]) && cwd[1] == L':') {
        wchar_t name[4] = { L'=', towupper(cwd[0]), L':', L'\0' };
        SetEnvironmentVariableW(name, cwd);
    }
}

static bool push_dir_stack(shell_ctx_t *ctx, const wchar_t *dir) {
    if (ctx->dir_stack_len == ctx->dir_stack_cap) {
        size_t new_cap = ctx->dir_stack_cap ? ctx->dir_stack_cap * 2 : 16;
        wchar_t **nb = (wchar_t **)realloc(ctx->dir_stack, new_cap * sizeof(wchar_t *));
        if (!nb) return false;
        ctx->dir_stack = nb;
        ctx->dir_stack_cap = new_cap;
    }
    wchar_t *copy = _wcsdup(dir);
    if (!copy) return false;
    ctx->dir_stack[ctx->dir_stack_len++] = copy;
    return true;
}

static const wchar_t *skip_ws(const wchar_t *p) {
    while (*p == L' ' || *p == L'\t') p++;
    return p;
}

static void trim_trailing_ws(wchar_t *s) {
    size_t n = wcslen(s);
    while (n > 0 && (s[n - 1] == L' ' || s[n - 1] == L'\t' || s[n - 1] == L'\r' || s[n - 1] == L'\n')) {
        s[--n] = L'\0';
    }
}

static void strip_outer_quotes_inplace(wchar_t *s) {
    size_t n = wcslen(s);
    if (n >= 2 && s[0] == L'"' && s[n - 1] == L'"') {
        memmove(s, s + 1, (n - 2) * sizeof(wchar_t));
        s[n - 2] = L'\0';
    }
}

static int exec_native_fastpath(shell_ctx_t *ctx, const wchar_t *line) {
    const wchar_t *p = skip_ws(line);

    /* Drive switch X: */
    if (iswalpha(p[0]) && p[1] == L':' && (*skip_ws(p + 2) == L'\0')) {
        wchar_t drive_var[4] = { L'=', towupper(p[0]), L':', L'\0' };
        wchar_t target[MAX_PATH * 4];
        DWORD n = GetEnvironmentVariableW(drive_var, target, (DWORD)(sizeof(target) / sizeof(target[0])));
        if (n == 0 || n >= sizeof(target) / sizeof(target[0])) {
            target[0] = towupper(p[0]);
            target[1] = L':';
            target[2] = L'\\';
            target[3] = L'\0';
        }
        if (!SetCurrentDirectoryW(target)) {
            fwprintf(stderr, L"The system cannot find the drive specified.\n");
            ctx->last_exit_code = 1;
            return 1;
        }
        ctx->last_exit_code = 0;
        return 0;
    }

    /* Extract command word */
    wchar_t cmd[32];
    size_t ci = 0;
    while (*p && *p != L' ' && *p != L'\t' && ci + 1 < sizeof(cmd) / sizeof(cmd[0])) {
        cmd[ci++] = *p++;
    }
    cmd[ci] = L'\0';
    const wchar_t *args = skip_ws(p);

    if (_wcsicmp(cmd, L"exit") == 0) {
        ctx->should_exit = true;
        if (*args) {
            if (_wcsnicmp(args, L"/b", 2) == 0 && (args[2] == L'\0' || args[2] == L' ' || args[2] == L'\t')) {
                args = skip_ws(args + 2);
            }
            if (*args) ctx->last_exit_code = _wtoi(args);
        }
        return ctx->last_exit_code;
    }

    if (_wcsicmp(cmd, L"cd") == 0 || _wcsicmp(cmd, L"chdir") == 0) {
        if (*args == L'\0') {
            wchar_t cwd[MAX_PATH * 4];
            if (GetCurrentDirectoryW((DWORD)(sizeof(cwd) / sizeof(cwd[0])), cwd) > 0) {
                wprintf(L"%ls\n", cwd);
            }
            ctx->last_exit_code = 0;
            return 0;
        }
        bool change_drive = true; /* Default convenient behavior; /d accepted */
        if (_wcsnicmp(args, L"/d", 2) == 0 && (args[2] == L' ' || args[2] == L'\t')) {
            args = skip_ws(args + 2);
            change_drive = true;
        }
        wchar_t target[MAX_PATH * 4];
        wcsncpy(target, args, (sizeof(target) / sizeof(target[0])) - 1);
        target[(sizeof(target) / sizeof(target[0])) - 1] = L'\0';
        trim_trailing_ws(target);
        strip_outer_quotes_inplace(target);

        (void)change_drive;
        if (!SetCurrentDirectoryW(target)) {
            fwprintf(stderr, L"The system cannot find the path specified.\n");
            ctx->last_exit_code = 1;
            return 1;
        }
        wchar_t new_cwd[MAX_PATH * 4];
        if (GetCurrentDirectoryW((DWORD)(sizeof(new_cwd) / sizeof(new_cwd[0])), new_cwd) > 0) {
            update_drive_cwd_env(new_cwd);
        }
        ctx->last_exit_code = 0;
        return 0;
    }

    if (_wcsicmp(cmd, L"pushd") == 0) {
        wchar_t old_cwd[MAX_PATH * 4];
        if (GetCurrentDirectoryW((DWORD)(sizeof(old_cwd) / sizeof(old_cwd[0])), old_cwd) == 0) {
            ctx->last_exit_code = 1;
            return 1;
        }
        if (*args == L'\0') {
            for (size_t i = ctx->dir_stack_len; i > 0; i--) {
                wprintf(L"%ls\n", ctx->dir_stack[i - 1]);
            }
            ctx->last_exit_code = 0;
            return 0;
        }
        wchar_t target[MAX_PATH * 4];
        wcsncpy(target, args, (sizeof(target) / sizeof(target[0])) - 1);
        target[(sizeof(target) / sizeof(target[0])) - 1] = L'\0';
        trim_trailing_ws(target);
        strip_outer_quotes_inplace(target);
        if (!SetCurrentDirectoryW(target)) {
            fwprintf(stderr, L"The system cannot find the path specified.\n");
            ctx->last_exit_code = 1;
            return 1;
        }
        push_dir_stack(ctx, old_cwd);
        wchar_t new_cwd[MAX_PATH * 4];
        if (GetCurrentDirectoryW((DWORD)(sizeof(new_cwd) / sizeof(new_cwd[0])), new_cwd) > 0) {
            update_drive_cwd_env(new_cwd);
        }
        ctx->last_exit_code = 0;
        return 0;
    }

    if (_wcsicmp(cmd, L"popd") == 0) {
        if (ctx->dir_stack_len == 0) {
            ctx->last_exit_code = 0;
            return 0;
        }
        wchar_t *prev = ctx->dir_stack[--ctx->dir_stack_len];
        BOOL ok = SetCurrentDirectoryW(prev);
        if (ok) {
            update_drive_cwd_env(prev);
        }
        free(prev);
        ctx->last_exit_code = ok ? 0 : 1;
        return ctx->last_exit_code;
    }

    ctx->last_exit_code = 0;
    return 0;
}

static int exec_direct_exe(shell_ctx_t *ctx, const wchar_t *exe_path, const wchar_t *line) {
    const wchar_t *p = skip_ws(line);
    if (*p == L'"') {
        p++;
        while (*p && *p != L'"') p++;
        if (*p == L'"') p++;
    } else {
        while (*p && *p != L' ' && *p != L'\t') p++;
    }

    size_t elen = wcslen(exe_path);
    size_t alen = wcslen(p);
    wchar_t *cmdline = (wchar_t *)malloc((elen + 2 + alen + 1) * sizeof(wchar_t));
    if (!cmdline) {
        ctx->last_exit_code = 1;
        return 1;
    }
    cmdline[0] = L'"';
    memcpy(cmdline + 1, exe_path, elen * sizeof(wchar_t));
    cmdline[1 + elen] = L'"';
    memcpy(cmdline + 2 + elen, p, alen * sizeof(wchar_t));
    cmdline[2 + elen + alen] = L'\0';

    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof(si));
    ZeroMemory(&pi, sizeof(pi));
    si.cb = sizeof(si);

    SetConsoleCtrlHandler(NULL, TRUE);
    BOOL ok = CreateProcessW(exe_path, cmdline, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi);
    free(cmdline);

    if (!ok) {
        SetConsoleCtrlHandler(NULL, FALSE);
        ctx->last_exit_code = (int)GetLastError();
        return ctx->last_exit_code;
    }

    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD ec = 0;
    GetExitCodeProcess(pi.hProcess, &ec);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    SetConsoleCtrlHandler(NULL, FALSE);

    ctx->last_exit_code = (int)ec;
    return ctx->last_exit_code;
}

static bool starts_with_call_ci(const wchar_t *line) {
    const wchar_t *p = skip_ws(line);
    return _wcsnicmp(p, L"call", 4) == 0 && (p[4] == L' ' || p[4] == L'\t');
}

static wchar_t *expand_interactive_errorlevel(const wchar_t *line, int last_exit_code) {
    if (!line) return NULL;
    wchar_t dummy[8];
    if (GetEnvironmentVariableW(L"ERRORLEVEL", dummy, 8) > 0) {
        return _wcsdup(line);
    }

    wchar_t ec_str[32];
    swprintf(ec_str, 32, L"%d", last_exit_code);
    size_t ec_len = wcslen(ec_str);

    size_t cap = wcslen(line) + 64;
    wchar_t *out = (wchar_t *)malloc(cap * sizeof(wchar_t));
    if (!out) return NULL;
    size_t oi = 0;

    const wchar_t *p = line;
    while (*p) {
        if (*p == L'^' && p[1] == L'%') {
            if (oi + 2 >= cap) { cap *= 2; out = (wchar_t *)realloc(out, cap * sizeof(wchar_t)); if (!out) return NULL; }
            out[oi++] = *p++;
            out[oi++] = *p++;
            continue;
        }
        if (_wcsnicmp(p, L"%ERRORLEVEL%", 12) == 0) {
            if (oi + ec_len + 1 >= cap) { cap = (cap + ec_len) * 2; out = (wchar_t *)realloc(out, cap * sizeof(wchar_t)); if (!out) return NULL; }
            memcpy(out + oi, ec_str, ec_len * sizeof(wchar_t));
            oi += ec_len;
            p += 12;
            continue;
        }
        if (oi + 2 >= cap) { cap *= 2; out = (wchar_t *)realloc(out, cap * sizeof(wchar_t)); if (!out) return NULL; }
        out[oi++] = *p++;
    }
    out[oi] = L'\0';
    return out;
}

static int exec_via_sync_bridge(shell_ctx_t *ctx, const wchar_t *line, bool is_batch_script) {
    wchar_t *effective_line = expand_interactive_errorlevel(line, ctx->last_exit_code);
    const wchar_t *cmd_body = effective_line ? effective_line : line;

    wchar_t temp_dir[MAX_PATH];
    DWORD tlen = GetTempPathW(MAX_PATH, temp_dir);
    if (tlen == 0 || tlen >= MAX_PATH) wcscpy(temp_dir, L".\\");

    wchar_t sync_file[MAX_PATH * 2];
    static unsigned long seq = 0;
    swprintf(sync_file, sizeof(sync_file) / sizeof(sync_file[0]),
             L"%lsplumbus_sync_%lu_%lu.bin",
             temp_dir, (unsigned long)GetCurrentProcessId(), ++seq);

    const wchar_t *call_prefix = (is_batch_script && !starts_with_call_ci(cmd_body)) ? L"call " : L"";
    size_t blen = wcslen(ctx->backend_path);
    size_t llen = wcslen(cmd_body);
    size_t slen = wcslen(ctx->self_path);
    size_t flen = wcslen(sync_file);

    /*
     * Empirically validated Phase 0 Sync Bridge template (/v:off + call ^%ERRORLEVEL^%):
     * "<backend>" /d /q /c "<call_prefix><line>& call "<self>" --__sync-state "<sync_file>" ^%ERRORLEVEL^%"
     */
    size_t cap = blen + llen + slen + flen + 128;
    wchar_t *cmdline = (wchar_t *)malloc(cap * sizeof(wchar_t));
    if (!cmdline) {
        free(effective_line);
        ctx->last_exit_code = 1;
        return 1;
    }

    swprintf(cmdline, cap,
             L"\"%ls\" /d /q /c \"%ls%ls& call \"%ls\" --__sync-state \"%ls\" ^%%ERRORLEVEL^%%\"",
             ctx->backend_path, call_prefix, cmd_body, ctx->self_path, sync_file);
    free(effective_line);

    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof(si));
    ZeroMemory(&pi, sizeof(pi));
    si.cb = sizeof(si);

    SetConsoleCtrlHandler(NULL, TRUE);
    BOOL ok = CreateProcessW(ctx->backend_path, cmdline, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi);
    free(cmdline);

    if (!ok) {
        SetConsoleCtrlHandler(NULL, FALSE);
        ctx->last_exit_code = (int)GetLastError();
        return ctx->last_exit_code;
    }

    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD proc_ec = 0;
    GetExitCodeProcess(pi.hProcess, &proc_ec);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    SetConsoleCtrlHandler(NULL, FALSE);

    synced_state_t state;
    if (state_sync_read_file(sync_file, &state)) {
        state_sync_apply(&state);
        ctx->last_exit_code = state.exit_code;
        state_sync_free(&state);
    } else {
        DeleteFileW(sync_file);
        ctx->last_exit_code = (int)proc_ec;
    }

    return ctx->last_exit_code;
}

static bool cwd_has_config_command(void) {
    const wchar_t *exts[] = { L"config.exe", L"config.bat", L"config.cmd", L"config.com" };
    for (int i = 0; i < 4; i++) {
        DWORD a = GetFileAttributesW(exts[i]);
        if (a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY)) return true;
    }
    return false;
}

static bool try_exec_shell_builtin(shell_ctx_t *ctx, const wchar_t *p) {
    plumbus_config_t *cfg = (plumbus_config_t *)ctx->config_ptr;
    if (!cfg) return false;

    wchar_t trimmed[512];
    wcsncpy(trimmed, p, 511);
    trimmed[511] = L'\0';
    trim_trailing_ws(trimmed);

    bool is_plumbus_cfg =
        (_wcsnicmp(trimmed, L"plumbus", 7) == 0 && (trimmed[7] == L' ' || trimmed[7] == L'\t') &&
         _wcsicmp(skip_ws(trimmed + 7), L"config") == 0) ||
        (_wcsnicmp(trimmed, L"plumbus.exe", 11) == 0 && (trimmed[11] == L' ' || trimmed[11] == L'\t') &&
         _wcsicmp(skip_ws(trimmed + 11), L"config") == 0);
    bool is_bare_cfg = (_wcsicmp(trimmed, L"config") == 0 && !cwd_has_config_command());
    if (is_plumbus_cfg || is_bare_cfg) {
        wchar_t cwd[MAX_PATH * 4];
        if (GetCurrentDirectoryW((DWORD)(sizeof(cwd) / sizeof(cwd[0])), cwd) == 0) wcscpy(cwd, L"~");
        git_status_t git_st = { 0 };
        if (cfg->git_status) git_native_inspect(cwd, &git_st);
        console_state_t con;
        console_init(&con);
        console_enter_raw(&con);
        tui_cfg_run(cfg, cwd, ctx->last_exit_code, &git_st);
        console_restore(&con);
        ctx->last_exit_code = 0;
        return true;
    }

    if (_wcsnicmp(p, L"alias", 5) == 0 && (p[5] == L'\0' || p[5] == L' ' || p[5] == L'\t')) {
        const wchar_t *args = skip_ws(p + 5);
        if (!*args) {
            for (size_t i = 0; i < cfg->alias_count; i++) {
                wprintf(L"%ls=%ls\n", cfg->aliases[i].name, cfg->aliases[i].value);
            }
            ctx->last_exit_code = 0;
            return true;
        }
        const wchar_t *eq = wcschr(args, L'=');
        if (eq) {
            size_t nlen = (size_t)(eq - args);
            wchar_t name[256];
            if (nlen > 0 && nlen < 256) {
                memcpy(name, args, nlen * sizeof(wchar_t));
                name[nlen] = L'\0';
                trim_trailing_ws(name);
                wchar_t *val = _wcsdup(skip_ws(eq + 1));
                if (val) {
                    trim_trailing_ws(val);
                    strip_outer_quotes_inplace(val);
                    config_set_alias(cfg, name, val);
                    free(val);
                }
            }
            ctx->last_exit_code = 0;
            return true;
        }
        const wchar_t *found = config_find_alias(cfg, args);
        if (found) {
            wprintf(L"%ls=%ls\n", args, found);
            ctx->last_exit_code = 0;
        } else {
            ctx->last_exit_code = 1;
        }
        return true;
    }

    if (_wcsnicmp(p, L"unalias", 7) == 0 && (p[7] == L' ' || p[7] == L'\t')) {
        wchar_t name[256];
        wcsncpy(name, skip_ws(p + 7), 255);
        name[255] = L'\0';
        trim_trailing_ws(name);
        ctx->last_exit_code = config_remove_alias(cfg, name) ? 0 : 1;
        return true;
    }

    if (_wcsnicmp(p, L"theme", 5) == 0 && (p[5] == L'\0' || p[5] == L' ' || p[5] == L'\t')) {
        const wchar_t *arg = skip_ws(p + 5);
        if (!*arg) {
            wprintf(L"%ls\n", cfg->theme == THEME_AGNOSTER ? L"agnoster" : L"robbyrussell");
        } else if (_wcsicmp(arg, L"agnoster") == 0) {
            cfg->theme = THEME_AGNOSTER;
        } else if (_wcsicmp(arg, L"robbyrussell") == 0) {
            cfg->theme = THEME_ROBBYRUSSELL;
        } else {
            fwprintf(stderr, L"Unknown theme: %ls (available: robbyrussell, agnoster)\n", arg);
            ctx->last_exit_code = 1;
            return true;
        }
        ctx->last_exit_code = 0;
        return true;
    }

    return false;
}

int exec_interactive_line(shell_ctx_t *ctx, const wchar_t *line) {
    if (!ctx || !line) return 0;

    const wchar_t *p = skip_ws(line);
    if (!*p) return ctx->last_exit_code;

    if (try_exec_shell_builtin(ctx, p)) {
        return ctx->last_exit_code;
    }

    wchar_t *expanded = NULL;
    if (ctx->config_ptr) {
        expanded = config_expand_alias_line((const plumbus_config_t *)ctx->config_ptr, p);
        if (expanded) p = expanded;
    }

    cmd_resolution_t res;
    resolver_classify_and_find(p, &res);

    int rc = 0;
    switch (res.kind) {
        case CMD_KIND_EMPTY:
            rc = ctx->last_exit_code;
            break;
        case CMD_KIND_NATIVE_FASTPATH:
            rc = exec_native_fastpath(ctx, p);
            break;
        case CMD_KIND_EXTERNAL_EXE:
            rc = exec_direct_exe(ctx, res.resolved_path, p);
            break;
        case CMD_KIND_BATCH_SCRIPT:
            rc = exec_via_sync_bridge(ctx, p, true);
            break;
        case CMD_KIND_CMD_INTERNAL:
        case CMD_KIND_CMD_COMPOUND:
        default:
            rc = exec_via_sync_bridge(ctx, p, false);
            break;
    }

    free(expanded);
    return rc;
}
