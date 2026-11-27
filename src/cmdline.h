#ifndef PLUMBUS_CMDLINE_H
#define PLUMBUS_CMDLINE_H

#include <stdbool.h>
#include <wchar.h>

typedef enum {
    CMD_MODE_INTERACTIVE = 0,
    CMD_MODE_EXEC_EXIT,      /* /c */
    CMD_MODE_EXEC_KEEP,      /* /k */
    CMD_MODE_HELP,           /* /? */
    CMD_MODE_CONFIG,         /* config */
    CMD_MODE_INTERNAL_SYNC,  /* --__sync-state <file> <exitcode> */
    CMD_MODE_BENCH_INIT      /* --__bench-init */
} cmd_mode_t;

typedef struct {
    cmd_mode_t mode;
    bool strip_quotes;          /* /s */
    bool echo_off;              /* /q */
    bool disable_autorun;       /* /d */
    bool output_unicode;        /* /u */
    bool delayed_expansion_set; /* /v:on or /v:off */
    bool delayed_expansion;
    bool extensions_set;        /* /e:on or /e:off */
    bool extensions;
    bool completion_set;        /* /f:on or /f:off */
    bool completion_enabled;
    wchar_t color_attr[4];      /* /t:fg */

    const wchar_t *raw_after_exe; /* Pointer into original raw_cmdline right after argv[0] */
    const wchar_t *command_raw;   /* Pointer into original raw_cmdline right after /c or /k */
    wchar_t *command_parsed;      /* Heap-allocated command after cmd.exe quote rules */

    wchar_t *sync_file_path;      /* Heap-allocated path for --__sync-state */
    int sync_exit_code;
} cmd_options_t;

/*
 * Parses a raw Windows command line (from GetCommandLineW()) using exact cmd.exe rules.
 * Caller must release resources with cmdline_free().
 */
bool cmdline_parse(const wchar_t *raw_cmdline, cmd_options_t *out_opts);

/*
 * Applies exact cmd.exe /c /k quote stripping rules to cmd_str.
 * Returns heap-allocated string (caller frees with free()).
 */
wchar_t *cmdline_strip_cmd_quotes(const wchar_t *cmd_str, bool force_strip_s);

void cmdline_free(cmd_options_t *opts);

#endif /* PLUMBUS_CMDLINE_H */
