# Plumbus Standalone `cmd.exe` — Complete Command & Function Map

This document maps all **42 internal commands** in Windows `cmd.exe`'s `.rdata` `JumpTable` (`0x14003A000`) and all **non-JumpTable parser/runtime constructs** from the Gamma PE Workbench export (`C:\Users\pedro\Downloads\cmd.exe.15cd209c99.gamma-re\cmd.exe_re`) to their clean-room C17 implementations in Plumbus on branch `feat/standalone-cmd`.

## Gamma RE Bundle Assessment

- **Analyzed binary:** `cmd.exe` (`10.0.26100.9549`, x64, `ImageBase = 0x140000000`, `EntryPoint = 0x140027B90`, SHA-256 `15cd209c995b7d3009c91ca40892d5b8f1fb162f3e5f10716642ea278e8e0d7b`).
- **Scope of export:** `manifest.json` lists **857** discovered functions; **150** key functions are decompiled into `functions/0x..._sub_....c`.
- **JumpTable layout (`.rdata` `0x14003A000`):** 42 entries, stride `32` bytes:
  `{ LPCWSTR name_ptr; void *func_ptr; uint32_t flags; uint32_t msg_id; uint32_t ext_msg_id; uint32_t pad; }`
- **Engineering approach:** Most individual command handlers in `JumpTable` are small dispatch wrappers not included in the 150 decompiled `.c` files, while shared core engine helpers (tokenizer, parser, variable expander, directory/path helpers, console I/O, `DATE`, `TIME`, `PATH`, `DPATH`, `ENDLOCAL`, `PROMPT`, `EXIT`, `PUSHD`/`POPD` helpers) are decompiled. Plumbus implements every command and engine construct **clean-room in pure C17** using the Gamma RE export purely as an architectural reference.

---

## Table 1 — All 42 `cmd.exe` JumpTable Commands (`0x14003A000`)

| # | Command | Handler VA | Size | Flags | Worker VA / Gamma RE `.c` Reference | Plumbus C17 Implementation |
|---:|---|---|---:|---|---|---|
| 0 | `DIR` | `0x140015DE0` | 25 B | `0x0` | `0x140015E00` (690 B), formatter [sub_14000FFD0](file:///C:/Users/pedro/Downloads/cmd.exe.15cd209c99.gamma-re/cmd.exe_re/functions/0x000000014000FFD0_sub_14000FFD0.c) | `builtin_dir` ([builtins_fs.c](file:///c:/Users/pedro/Downloads/Plumbus/src/builtins_fs.c)) |
| 1 | `ERASE` | `0x140015C10` | 25 B | `0x0` | `0x140015C30` (424 B) | `builtin_del` ([builtins_fs.c](file:///c:/Users/pedro/Downloads/Plumbus/src/builtins_fs.c)) |
| 2 | `DEL` | `0x140015C10` | 25 B | `0x0` | `0x140015C30` (424 B, alias of `ERASE`) | `builtin_del` ([builtins_fs.c](file:///c:/Users/pedro/Downloads/Plumbus/src/builtins_fs.c)) |
| 3 | `TYPE` | `0x140032AB0` | 38 B | `0x2` | `0x140019988` (1147 B) | `builtin_type` ([builtins_fs.c](file:///c:/Users/pedro/Downloads/Plumbus/src/builtins_fs.c)) |
| 4 | `COPY` | `0x140032640` | 25 B | `0x1` | `0x1400224A4` (519 B) | `builtin_copy` ([builtins_fs.c](file:///c:/Users/pedro/Downloads/Plumbus/src/builtins_fs.c)) |
| 5 | `CD` | `0x14001FCF0` | 565 B | `0x1` | [sub_1400093D0](file:///C:/Users/pedro/Downloads/cmd.exe.15cd209c99.gamma-re/cmd.exe_re/functions/0x00000001400093D0_sub_1400093D0.c), [sub_140009F84](file:///C:/Users/pedro/Downloads/cmd.exe.15cd209c99.gamma-re/cmd.exe_re/functions/0x0000000140009F84_sub_140009F84.c) | `builtin_cd` ([builtins_fs.c](file:///c:/Users/pedro/Downloads/Plumbus/src/builtins_fs.c)) |
| 6 | `CHDIR` | `0x14001FCF0` | 565 B | `0x1` | Same as `CD` | `builtin_cd` ([builtins_fs.c](file:///c:/Users/pedro/Downloads/Plumbus/src/builtins_fs.c)) |
| 7 | `RENAME` | `0x140032870` | 21 B | `0x3` | `0x140031E00` (2097 B) | `builtin_ren` ([builtins_fs.c](file:///c:/Users/pedro/Downloads/Plumbus/src/builtins_fs.c)) |
| 8 | `REN` | `0x140032870` | 21 B | `0x3` | Same as `RENAME` | `builtin_ren` ([builtins_fs.c](file:///c:/Users/pedro/Downloads/Plumbus/src/builtins_fs.c)) |
| 9 | `ECHO` | `0x140010D30` | 165 B | `0x0` | [sub_140010DDC](file:///C:/Users/pedro/Downloads/cmd.exe.15cd209c99.gamma-re/cmd.exe_re/functions/0x0000000140010DDC_sub_140010DDC.c), [sub_1400121A0](file:///C:/Users/pedro/Downloads/cmd.exe.15cd209c99.gamma-re/cmd.exe_re/functions/0x00000001400121A0_sub_1400121A0.c) | `builtin_echo` ([builtins_sys.c](file:///c:/Users/pedro/Downloads/Plumbus/src/builtins_sys.c)) |
| 10 | `SET` | `0x14001C5F0` | 46 B | `0x0` | `0x14001C624` (530 B) | `builtin_set` ([builtins_env.c](file:///c:/Users/pedro/Downloads/Plumbus/src/builtins_env.c)) |
| 11 | `PAUSE` | `0x14002E710` | 171 B | `0x0` | [sub_140010AF0](file:///C:/Users/pedro/Downloads/cmd.exe.15cd209c99.gamma-re/cmd.exe_re/functions/0x0000000140010AF0_sub_140010AF0.c), [sub_14001C14C](file:///C:/Users/pedro/Downloads/cmd.exe.15cd209c99.gamma-re/cmd.exe_re/functions/0x000000014001C14C_sub_14001C14C.c) | `builtin_pause` ([builtins_sys.c](file:///c:/Users/pedro/Downloads/Plumbus/src/builtins_sys.c)) |
| 12 | `DATE` | `0x14002EF70` | 249 B | `0x0` | [sub_1400127D4](file:///C:/Users/pedro/Downloads/cmd.exe.15cd209c99.gamma-re/cmd.exe_re/functions/0x00000001400127D4_sub_1400127D4.c) (1313 B) | `builtin_date` ([builtins_sys.c](file:///c:/Users/pedro/Downloads/Plumbus/src/builtins_sys.c)) |
| 13 | `TIME` | `0x14002F070` | 262 B | `0x0` | [sub_140013654](file:///C:/Users/pedro/Downloads/cmd.exe.15cd209c99.gamma-re/cmd.exe_re/functions/0x0000000140013654_sub_140013654.c) (897 B) | `builtin_time` ([builtins_sys.c](file:///c:/Users/pedro/Downloads/Plumbus/src/builtins_sys.c)) |
| 14 | `PROMPT` | `0x14002FBD0` | 86 B | `0x0` | [sub_140007990](file:///C:/Users/pedro/Downloads/cmd.exe.15cd209c99.gamma-re/cmd.exe_re/functions/0x0000000140007990_sub_140007990.c), [sub_1400141C4](file:///C:/Users/pedro/Downloads/cmd.exe.15cd209c99.gamma-re/cmd.exe_re/functions/0x00000001400141C4_sub_1400141C4.c) | `builtin_prompt` ([builtins_env.c](file:///c:/Users/pedro/Downloads/Plumbus/src/builtins_env.c)) |
| 15 | `MD` | `0x1400345D0` | 38 B | `0x2` | `0x140019988` (1147 B) | `builtin_mkdir` ([builtins_fs.c](file:///c:/Users/pedro/Downloads/Plumbus/src/builtins_fs.c)) |
| 16 | `MKDIR` | `0x1400345D0` | 38 B | `0x2` | Same as `MD` | `builtin_mkdir` ([builtins_fs.c](file:///c:/Users/pedro/Downloads/Plumbus/src/builtins_fs.c)) |
| 17 | `RD` | `0x1400261E0` | thunk | `0x0` | `0x140001CB8` (628 B), `0x140039010` (4080 B) | `builtin_rmdir` ([builtins_fs.c](file:///c:/Users/pedro/Downloads/Plumbus/src/builtins_fs.c)) |
| 18 | `RMDIR` | `0x1400261E0` | thunk | `0x0` | Same as `RD` | `builtin_rmdir` ([builtins_fs.c](file:///c:/Users/pedro/Downloads/Plumbus/src/builtins_fs.c)) |
| 19 | `PATH` | `0x14002FB90` | 51 B | `0x0` | [sub_14002F7D8](file:///C:/Users/pedro/Downloads/cmd.exe.15cd209c99.gamma-re/cmd.exe_re/functions/0x000000014002F7D8_sub_14002F7D8.c) (195 B) | `builtin_path` ([builtins_env.c](file:///c:/Users/pedro/Downloads/Plumbus/src/builtins_env.c)) |
| 20 | `GOTO` | `0x140022F60` | 1981 B | `0x0` | [sub_140022CF4](file:///C:/Users/pedro/Downloads/cmd.exe.15cd209c99.gamma-re/cmd.exe_re/functions/0x0000000140022CF4_sub_140022CF4.c) | `builtin_goto` ([cmd_runtime.c](file:///c:/Users/pedro/Downloads/Plumbus/src/cmd_runtime.c)) |
| 21 | `SHIFT` | `0x14002E7D0` | 309 B | `0x0` | [sub_14000B508](file:///C:/Users/pedro/Downloads/cmd.exe.15cd209c99.gamma-re/cmd.exe_re/functions/0x000000014000B508_sub_14000B508.c) | `builtin_shift` ([cmd_runtime.c](file:///c:/Users/pedro/Downloads/Plumbus/src/cmd_runtime.c)) |
| 22 | `CLS` | `0x1400139E0` | 442 B | `0x2` | [sub_1400121A0](file:///C:/Users/pedro/Downloads/cmd.exe.15cd209c99.gamma-re/cmd.exe_re/functions/0x00000001400121A0_sub_1400121A0.c) | `builtin_cls` ([builtins_sys.c](file:///c:/Users/pedro/Downloads/Plumbus/src/builtins_sys.c)) |
| 23 | `CALL` | `0x14002E660` | 25 B | `0x0` | `0x14001B728` (518 B) | `builtin_call` ([cmd_runtime.c](file:///c:/Users/pedro/Downloads/Plumbus/src/cmd_runtime.c)) |
| 24 | `VERIFY` | `0x140033DC0` | 85 B | `0x2` | [sub_140010DDC](file:///C:/Users/pedro/Downloads/cmd.exe.15cd209c99.gamma-re/cmd.exe_re/functions/0x0000000140010DDC_sub_140010DDC.c) | `builtin_verify` ([builtins_sys.c](file:///c:/Users/pedro/Downloads/Plumbus/src/builtins_sys.c)) |
| 25 | `VER` | `0x140010A70` | 119 B | `0x2` | [sub_140010B28](file:///C:/Users/pedro/Downloads/cmd.exe.15cd209c99.gamma-re/cmd.exe_re/functions/0x0000000140010B28_sub_140010B28.c) | `builtin_ver` ([builtins_sys.c](file:///c:/Users/pedro/Downloads/Plumbus/src/builtins_sys.c)) |
| 26 | `VOL` | `0x140032AE0` | 38 B | `0x2` | `0x140019988` (1147 B) | `builtin_vol` ([builtins_sys.c](file:///c:/Users/pedro/Downloads/Plumbus/src/builtins_sys.c)) |
| 27 | `EXIT` | `0x14001EAA0` | 256 B | `0x0` | [sub_14001EBA8](file:///C:/Users/pedro/Downloads/cmd.exe.15cd209c99.gamma-re/cmd.exe_re/functions/0x000000014001EBA8_sub_14001EBA8.c) | `builtin_exit` ([cmd_runtime.c](file:///c:/Users/pedro/Downloads/Plumbus/src/cmd_runtime.c)) |
| 28 | `SETLOCAL` | `0x14001C250` | 580 B | `0x0` | [sub_1400040A0](file:///C:/Users/pedro/Downloads/cmd.exe.15cd209c99.gamma-re/cmd.exe_re/functions/0x00000001400040A0_sub_1400040A0.c) | `builtin_setlocal` ([builtins_env.c](file:///c:/Users/pedro/Downloads/Plumbus/src/builtins_env.c)) |
| 29 | `ENDLOCAL` | `0x14002E6B0` | 24 B | `0x0` | [sub_14001C49C](file:///C:/Users/pedro/Downloads/cmd.exe.15cd209c99.gamma-re/cmd.exe_re/functions/0x000000014001C49C_sub_14001C49C.c) (245 B) | `builtin_endlocal` ([builtins_env.c](file:///c:/Users/pedro/Downloads/Plumbus/src/builtins_env.c)) |
| 30 | `TITLE` | `0x14001ED80` | 566 B | `0x0` | `0x14001F020` (231 B), [sub_14000B508](file:///C:/Users/pedro/Downloads/cmd.exe.15cd209c99.gamma-re/cmd.exe_re/functions/0x000000014000B508_sub_14000B508.c) | `builtin_title` ([builtins_sys.c](file:///c:/Users/pedro/Downloads/Plumbus/src/builtins_sys.c)) |
| 31 | `START` | `0x140022C80` | thunk | `0x0` | `0x140020C64` (4520 B) | `builtin_start` ([builtins_sys.c](file:///c:/Users/pedro/Downloads/Plumbus/src/builtins_sys.c)) |
| 32 | `DPATH` | `0x14002FB50` | 48 B | `0x0` | [sub_14002F7D8](file:///C:/Users/pedro/Downloads/cmd.exe.15cd209c99.gamma-re/cmd.exe_re/functions/0x000000014002F7D8_sub_14002F7D8.c) (195 B) | `builtin_dpath` ([builtins_env.c](file:///c:/Users/pedro/Downloads/Plumbus/src/builtins_env.c)) |
| 33 | `KEYS` | `0x140032C10` | 244 B | `0x2` | [sub_140007990](file:///C:/Users/pedro/Downloads/cmd.exe.15cd209c99.gamma-re/cmd.exe_re/functions/0x0000000140007990_sub_140007990.c) | `builtin_keys` ([builtins_sys.c](file:///c:/Users/pedro/Downloads/Plumbus/src/builtins_sys.c)) |
| 34 | `MOVE` | `0x140032660` | 514 B | `0x1` | `0x140038890` | `builtin_move` ([builtins_fs.c](file:///c:/Users/pedro/Downloads/Plumbus/src/builtins_fs.c)) |
| 35 | `PUSHD` | `0x140021E20` | 969 B | `0x3` | [sub_140008F10](file:///C:/Users/pedro/Downloads/cmd.exe.15cd209c99.gamma-re/cmd.exe_re/functions/0x0000000140008F10_sub_140008F10.c) | `builtin_pushd` ([builtins_fs.c](file:///c:/Users/pedro/Downloads/Plumbus/src/builtins_fs.c)) |
| 36 | `POPD` | `0x14000E9F0` | 88 B | `0x3` | [sub_1400093D0](file:///C:/Users/pedro/Downloads/cmd.exe.15cd209c99.gamma-re/cmd.exe_re/functions/0x00000001400093D0_sub_1400093D0.c) | `builtin_popd` ([builtins_fs.c](file:///c:/Users/pedro/Downloads/Plumbus/src/builtins_fs.c)) |
| 37 | `ASSOC` | `0x1400314D0` | 46 B | `0x4` | `0x14002FC84` (384 B) | `builtin_assoc` ([builtins_sys.c](file:///c:/Users/pedro/Downloads/Plumbus/src/builtins_sys.c)) |
| 38 | `FTYPE` | `0x140031510` | 46 B | `0x4` | `0x140030060` (376 B) | `builtin_ftype` ([builtins_sys.c](file:///c:/Users/pedro/Downloads/Plumbus/src/builtins_sys.c)) |
| 39 | `BREAK` | `0x14002E650` | thunk | `0x0` | Legacy no-op | `builtin_break` ([builtins_sys.c](file:///c:/Users/pedro/Downloads/Plumbus/src/builtins_sys.c)) |
| 40 | `COLOR` | `0x140033CD0` | 232 B | `0x4` | [sub_140012600](file:///C:/Users/pedro/Downloads/cmd.exe.15cd209c99.gamma-re/cmd.exe_re/functions/0x0000000140012600_sub_140012600.c) | `builtin_color` ([builtins_sys.c](file:///c:/Users/pedro/Downloads/Plumbus/src/builtins_sys.c)) |
| 41 | `MKLINK` | `0x1400385A0` | 25 B | `0x0` | `0x140037BD4` (332 B) | `builtin_mklink` ([builtins_fs.c](file:///c:/Users/pedro/Downloads/Plumbus/src/builtins_fs.c)) |

---

## Table 2 — Non-JumpTable Parser & Core Engine Constructs

| Construct / Subsystem | `cmd.exe` VA | Gamma RE `.c` Reference | Plumbus C17 Module |
|---|---|---|---|
| Lexer / Tokenizer (`GeToken`) | `0x1400060F0` (2295 B) | [sub_1400060F0](file:///C:/Users/pedro/Downloads/cmd.exe.15cd209c99.gamma-re/cmd.exe_re/functions/0x00000001400060F0_sub_1400060F0.c) | [cmd_ast.c](file:///c:/Users/pedro/Downloads/Plumbus/src/cmd_ast.c) |
| AST Parser (`IF`, `FOR`, `REM`, `::`, `( )`, `&`, `&&`, `\|\|`, `\|`, redirs) | `0x14003327C` (1183 B) | [sub_14003327C](file:///C:/Users/pedro/Downloads/cmd.exe.15cd209c99.gamma-re/cmd.exe_re/functions/0x000000014003327C_sub_14003327C.c) | [cmd_ast.c](file:///c:/Users/pedro/Downloads/Plumbus/src/cmd_ast.c) |
| `IF` / `FOR` / AST Unparse & Evaluation | `0x140013BF0`, `0x140033ED8` | [sub_140013BF0](file:///C:/Users/pedro/Downloads/cmd.exe.15cd209c99.gamma-re/cmd.exe_re/functions/0x0000000140013BF0_sub_140013BF0.c), [sub_140033ED8](file:///C:/Users/pedro/Downloads/cmd.exe.15cd209c99.gamma-re/cmd.exe_re/functions/0x0000000140033ED8_sub_140033ED8.c) | [cmd_runtime.c](file:///c:/Users/pedro/Downloads/Plumbus/src/cmd_runtime.c) |
| JumpTable Lookup & Help Dispatch (`FindCmd`) | `0x140004890` (335 B) | [sub_140004890](file:///C:/Users/pedro/Downloads/cmd.exe.15cd209c99.gamma-re/cmd.exe_re/functions/0x0000000140004890_sub_140004890.c) | [builtins.c](file:///c:/Users/pedro/Downloads/Plumbus/src/builtins.c) |
| Env & Dynamic Pseudo-Vars (`GetEnvVar`: `CD`, `ERRORLEVEL`, `CMDEXTVERSION`, `CMDCMDLINE`, `DATE`, `TIME`, `RANDOM`, `HIGHESTNUMANODENUMBER`) | `0x14000BC40` (629 B) | [sub_14000BC40](file:///C:/Users/pedro/Downloads/cmd.exe.15cd209c99.gamma-re/cmd.exe_re/functions/0x000000014000BC40_sub_14000BC40.c) | [cmd_expand.c](file:///c:/Users/pedro/Downloads/Plumbus/src/cmd_expand.c) |
| Executable Search (`SearchForExecutable`) | `0x14000A298` (1112 B) | [sub_14000A298](file:///C:/Users/pedro/Downloads/cmd.exe.15cd209c99.gamma-re/cmd.exe_re/functions/0x000000014000A298_sub_14000A298.c) | [resolver.c](file:///c:/Users/pedro/Downloads/Plumbus/src/resolver.c), [cmd_runtime.c](file:///c:/Users/pedro/Downloads/Plumbus/src/cmd_runtime.c) |
| Batch File Reader (`ReadBufFromFile`) | `0x14000F794` (685 B) | [sub_14000F794](file:///C:/Users/pedro/Downloads/cmd.exe.15cd209c99.gamma-re/cmd.exe_re/functions/0x000000014000F794_sub_14000F794.c) | [cmd_runtime.c](file:///c:/Users/pedro/Downloads/Plumbus/src/cmd_runtime.c) |
| Console/File Unicode Output | `0x140011D94` (834 B) | [sub_140011D94](file:///C:/Users/pedro/Downloads/cmd.exe.15cd209c99.gamma-re/cmd.exe_re/functions/0x0000000140011D94_sub_140011D94.c) | [builtins.c](file:///c:/Users/pedro/Downloads/Plumbus/src/builtins.c) |
