# Arquitetura do Plumbus

## Visão Geral
O **Plumbus** é um shell Windows escrito estritamente em **C17** (`wchar_t` / Win32 `W` APIs), linkado estaticamente com a CRT (`/MT`) para produzir um único executável `< 500 KB` sem DLLs externas.

```text
GetCommandLineW()
   │
   ▼
cmdline_parse() ──► [--__sync-state] ──► state_sync_write_current()
   │
   ▼
backend_resolve_cmd() + anti-recursão física (GetFileInformationByHandle)
   │
   ├──► (/c, /?, stdin redirecionado) ──► proxy_delegate_raw()
   │
   └──► (Console interativo / /k híbrido)
           │
           ▼
        REPL TUI (line_editor + highlight + completion + history + git_native + prompt)
           │
           ▼
        exec_interactive_line()
           ├──► Built-ins (plumbus config, alias, unalias, theme)
           ├──► Fast-path nativo (cd, X:, pushd, popd, exit)
           ├──► .exe/.com direto (CreateProcessW)
           └──► Internos / .bat / compostos ──► Sync Bridge (/v:off + --__sync-state)
```

---

## Módulos (`src/`)

| Módulo | Responsabilidade |
|---|---|
| `cmdline.c` | Parser bruto sobre `GetCommandLineW()` preservando regras exatas de aspas (`/c`, `/k`, `/s`) |
| `backend.c` | Resolução do backend (`PLUMBUS_CMD_BACKEND` -> `./backup/cmd.exe` em dev -> `System32\cmd.exe` em prod) e proteção anti-loop |
| `autorun.c` | Leitura `KEY_READ` de `AutoRun` em `HKLM` e `HKCU` (suprimido com `/d`) |
| `proxy.c` | Detecção de console interativo (`GetConsoleMode`) e delegação transparente de handles + `ExitCode` |
| `resolver.c` | Precedência `aliases -> internos CMD -> CWD -> %PATH% × %PATHEXT%` e classificação de comandos |
| `state_sync.c` | Protocolo binário UTF-16LE (`magic 0x504C5359`) para sincronizar `CWD`, `GetEnvironmentStringsW()` (incluindo `=X:`) e `ExitCode` |
| `exec.c` | Executor híbrido + expansão de `%ERRORLEVEL%` entre comandos interativos |
| `console.c` | Gerenciamento de modos raw/cooked e `ENABLE_VIRTUAL_TERMINAL_PROCESSING` |
| `history.c` | Histórico UTF-8 em `%LocalAppData%\plumbus\history`, *ghost text* e busca reversa (`Ctrl+R`) |
| `completion.c` | Autocompletar cíclico para arquivos/pastas, `%VAR%` e subcomandos `git` |
| `highlight.c` | Colorização ANSI 24-bit em tempo real |
| `line_editor.c` | Editor de linha desacoplado (`line_buf_t` + `ReadConsoleInputW`) com suporte a `F2` / `Ctrl+,` |
| `git_native.c` | Parser Git em C puro (`HEAD`, `DIRC` v2/v3/v4, `.gitignore`, `GetFileAttributesExW`, `FindFirstFileW`) |
| `config.c` | Leitura e gravação (`config_save`) de `%USERPROFILE%\.cmdrc` |
| `prompt.c` | Temas `robbyrussell` e `agnoster`, abreviação `~` e largura visível CSI |
| `tui_config.c` | Configurador visual em *Alternate Screen Buffer* com Live Preview |
