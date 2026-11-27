#include "cmdline.h"
#include "autorun.h"
#include "proxy.h"
#include "exec.h"
#include "builtins.h"
#include "console.h"
#include "history.h"
#include "line_editor.h"
#include "git_native.h"
#include "config.h"
#include "prompt.h"
#include "tui_config.h"
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>

static bool alias_lookup_cb(const wchar_t *name, void *user_data) {
    const plumbus_config_t *cfg = (const plumbus_config_t *)user_data;
    if (_wcsicmp(name, L"plumbus") == 0 || _wcsicmp(name, L"config") == 0 ||
        _wcsicmp(name, L"alias") == 0 || _wcsicmp(name, L"unalias") == 0 ||
        _wcsicmp(name, L"theme") == 0) {
        return true;
    }
    return cfg && config_find_alias(cfg, name) != NULL;
}

typedef struct {
    plumbus_config_t *cfg;
    const wchar_t *cwd;
    int last_exit_code;
    const git_status_t *git_st;
} repl_cfg_ctx_t;

static bool repl_open_config_cb(void *user_data,
                                wchar_t **new_prompt_ansi,
                                size_t *new_prompt_vis_len,
                                bool *out_enable_hl,
                                bool *out_enable_ghost) {
    repl_cfg_ctx_t *rc = (repl_cfg_ctx_t *)user_data;
    if (!rc || !rc->cfg) return false;
    bool saved = tui_cfg_run(rc->cfg, rc->cwd, rc->last_exit_code, rc->git_st);
    if (saved) {
        git_status_t g = { 0 };
        if (rc->cfg->git_status) git_native_inspect(rc->cwd, &g);
        wchar_t *np = prompt_render(rc->cfg, rc->cwd, rc->last_exit_code, rc->cfg->git_status ? &g : NULL);
        if (np) {
            *new_prompt_ansi = np;
            *new_prompt_vis_len = prompt_last_line_visible_width(np);
        }
        *out_enable_hl = rc->cfg->syntax_highlighting;
        *out_enable_ghost = rc->cfg->ghost_text;
    }
    return saved;
}

static void run_tui_repl(shell_ctx_t *ctx, plumbus_config_t *cfg) {
    console_state_t con;
    console_init(&con);

    history_t hist;
    history_init(&hist, 5000);

    while (!ctx->should_exit) {
        wchar_t cwd[MAX_PATH * 4];
        if (GetCurrentDirectoryW((DWORD)(sizeof(cwd) / sizeof(cwd[0])), cwd) == 0) {
            wcscpy(cwd, L"~");
        }

        git_status_t git_st = { 0 };
        if (cfg->git_status) {
            git_native_inspect(cwd, &git_st);
        }

        wchar_t *prompt = prompt_render(cfg, cwd, ctx->last_exit_code, cfg->git_status ? &git_st : NULL);
        const wchar_t *pstr = prompt ? prompt : L"> ";
        size_t pvis = prompt_last_line_visible_width(pstr);

        repl_cfg_ctx_t rctx = { cfg, cwd, ctx->last_exit_code, &git_st };

        console_enter_raw(&con);
        wchar_t *line = NULL;
        line_read_status_t st = line_editor_read(
            pstr, pvis, &hist,
            alias_lookup_cb, cfg,
            cfg->syntax_highlighting, cfg->ghost_text,
            repl_open_config_cb, &rctx,
            &line);
        console_restore(&con);
        free(prompt);

        if (st == LINE_READ_EOF) {
            break;
        }
        if (st == LINE_READ_ABORT || !line) {
            free(line);
            continue;
        }
        if (*line) {
            history_add(&hist, line);
            exec_interactive_line(ctx, line);
        }
        free(line);
    }

    history_free(&hist);
    console_restore(&con);
}

static void run_piped_stdin_repl(shell_ctx_t *ctx) {
    HANDLE h_in = GetStdHandle(STD_INPUT_HANDLE);
    wchar_t line[8192];
    while (!ctx->should_exit && cmd_io_read_line(h_in, line, sizeof(line) / sizeof(line[0]))) {
        if (line[0]) {
            exec_interactive_line(ctx, line);
        }
    }
}

static void print_standalone_help(void) {
    cmd_io_t io = cmd_io_std();
    cmd_io_write_sz(io.h_out,
        L"Plumbus Standalone Command Interpreter (C17)\r\n\r\n"
        L"PLUMBUS [/C string | /K string] [/D] [/Q] [/S] [/E:ON|OFF] [/V:ON|OFF]\r\n\r\n"
        L"  /C      Carries out the command specified by string and then terminates.\r\n"
        L"  /K      Carries out the command specified by string but remains.\r\n"
        L"  /D      Disable execution of AutoRun commands from registry.\r\n"
        L"  /Q      Turns echo off.\r\n"
        L"  /S      Modifies the treatment of string after /C or /K.\r\n"
        L"  /E:ON   Enable command extensions (default).\r\n"
        L"  /V:ON   Enable delayed environment variable expansion.\r\n"
        L"  config  Open interactive TUI Configurator (also F2 or Ctrl+,).\r\n");
}

int wmain(void) {
    cmd_options_t opts;
    cmdline_parse(GetCommandLineW(), &opts);

    if (opts.mode == CMD_MODE_BENCH_INIT || opts.mode == CMD_MODE_INTERNAL_SYNC) {
        cmdline_free(&opts);
        return 0;
    }

    if (opts.mode == CMD_MODE_HELP) {
        print_standalone_help();
        cmdline_free(&opts);
        return 0;
    }

    if (opts.mode == CMD_MODE_CONFIG) {
        plumbus_config_t cfg;
        config_init(&cfg);
        config_load_rc(&cfg);
        wchar_t cwd[MAX_PATH * 4];
        if (GetCurrentDirectoryW((DWORD)(sizeof(cwd) / sizeof(cwd[0])), cwd) == 0) wcscpy(cwd, L"~");
        git_status_t git_st = { 0 };
        if (cfg.git_status) git_native_inspect(cwd, &git_st);
        console_state_t con;
        console_init(&con);
        console_enter_raw(&con);
        tui_cfg_run(&cfg, cwd, 0, &git_st);
        console_restore(&con);
        config_free(&cfg);
        cmdline_free(&opts);
        return 0;
    }

    plumbus_config_t cfg;
    config_init(&cfg);
    config_load_rc(&cfg);

    shell_ctx_t ctx;
    if (!shell_ctx_init(&ctx, &opts, NULL)) {
        config_free(&cfg);
        cmdline_free(&opts);
        return 1;
    }
    ctx.config_ptr = &cfg;

    wchar_t *autorun = autorun_read_combined(opts.disable_autorun);
    if (autorun) {
        exec_interactive_line(&ctx, autorun);
        free(autorun);
    }

    if (opts.mode == CMD_MODE_EXEC_EXIT) {
        if (opts.command_parsed && opts.command_parsed[0]) {
            exec_interactive_line(&ctx, opts.command_parsed);
        }
        int ec = ctx.last_exit_code;
        if (ec == 9009 && !ctx.should_exit) ec = 1;
        shell_ctx_free(&ctx);
        config_free(&cfg);
        cmdline_free(&opts);
        return ec;
    }

    if (opts.mode == CMD_MODE_EXEC_KEEP && opts.command_parsed && opts.command_parsed[0]) {
        exec_interactive_line(&ctx, opts.command_parsed);
    }

    if (!ctx.should_exit) {
        if (proxy_is_stdin_interactive()) {
            run_tui_repl(&ctx, &cfg);
        } else {
            run_piped_stdin_repl(&ctx);
        }
    }

    int final_ec = ctx.last_exit_code;
    shell_ctx_free(&ctx);
    config_free(&cfg);
    cmdline_free(&opts);
    return final_ec;
}
