# Arquitetura do Plumbus

O **Plumbus** é estruturado em módulos independentes escritos em **C17 puro** (`wchar_t` + Win32 API `W`), sem alocadores externos ou runtime dinâmico.

---

## 1. Fluxo de Execução e Proxy Híbrido

```text
GetCommandLineW()
       │
       ▼
  cmdline_parse()
   ├── --__sync-state   ──▶ state_sync_write_current() (helper binário UTF-16LE)
   ├── --__bench-init   ──▶ exit(0) (benchmark de cold start)
   ├── /c | /? | pipe   ──▶ proxy_delegate_raw(backend_cmd, raw_after_exe)
   └── console interativo (ou /k em console)
           │
           ├── autorun_read_combined() (HKLM -> HKCU, suprimido por /d)
           ├── config_load_rc() (%USERPROFILE%\.cmdrc)
           └── REPL TUI (git_native -> prompt_render -> line_editor_read -> exec)
```

### Proteção Anti-Recursão (`src/backend.c`)
Antes de qualquer delegação, `backend_resolve_cmd()` resolve o backend nesta ordem:
1. `PLUMBUS_CMD_BACKEND` (override explícito para testes)
2. `./backup/cmd.exe` (apenas quando compilado com `DEV_USE_LOCAL_BACKUP=ON`)
3. `GetSystemDirectoryW() + "\cmd.exe"` (produção, somente leitura/execução)

Em seguida, compara `GetModuleFileNameW(NULL)` com o backend via `GetFileInformationByHandle()` (`dwVolumeSerialNumber` + `nFileIndexHigh/Low`), bloqueando imediatamente (`BACKEND_ERR_SELF_LOOP`) qualquer tentativa de auto-invocação recursiva — mesmo via `%ComSpec%`, caminhos 8.3, symlinks ou hardlinks.

---

## 2. Ponte de Sincronização de Estado (`src/state_sync.c` + `src/exec.c`)

Comandos externos (`.exe`/`.com`) sem metacaracteres rodam via `CreateProcessW()` direto, e `cd`/`pushd`/`popd`/`X:`/`exit` rodam em fast-path nativo em C.

Para comandos internos do CMD (`set`, `dir`, `copy`, etc.), compostos (`|`, `&&`, `>`) e scripts `.bat`/`.cmd` (como `vcvarsall.bat`):
1. O Plumbus executa o backend com `/d /q /c` preservando os handles reais do console (`CONIN$`/`CONOUT$`).
2. Para `.bat`/`.cmd`, prefixa `call` automaticamente para capturar retorno após `exit /b N`.
3. Encadeia ao final `& call "<plumbus.exe>" --__sync-state "<tmp.bin>" ^%ERRORLEVEL^%` (mantendo `/v:off` padrão para não corromper strings com `!`).
4. O helper `--__sync-state` grava um blob binário UTF-16LE (`magic 0x504C5359`, `exit_code`, `GetCurrentDirectoryW()`, `GetEnvironmentStringsW()`).
5. O processo pai lê e deleta o arquivo temporário, aplica `SetCurrentDirectoryW()` e faz diff atômico de ambiente via `SetEnvironmentVariableW()` (preservando variáveis de drive `=C:` e **nunca** criando variável literal `ERRORLEVEL`).

---

## 3. Parser Git Nativo em C (`src/git_native.c`)

Sem nunca chamar `git.exe`, `git_native_inspect()`:
1. Sobe a árvore de diretórios procurando `.git` (pasta ou arquivo `gitdir:` de worktree/submodule).
2. Lê `.git/HEAD` para extrair a branch (`ref: refs/heads/...`) ou hash curto de 7 caracteres em *detached HEAD*.
3. Faz parse binário de `.git/index` (`DIRC` v2, v3 e v4 com prefix compression) e indexa caminhos em tabela hash aberta.
4. Verifica modificações/exclusões via `GetFileAttributesExW()` (tamanho e `mtime`).
5. Carrega `.gitignore` e `.git/info/exclude` e varre o repositório com `FindFirstFileW`/`FindNextFileW` para detectar arquivos não rastreados.

---

## 4. Mapa de Módulos (`src/`)

| Módulo | Responsabilidade |
|---|---|
| `cmdline.c` | Parser bruto de `GetCommandLineW()`, flags e regras de aspas do `cmd.exe` |
| `backend.c` | Resolução segura de backend + proteção anti-recursão |
| `autorun.c` | Leitura `KEY_READ` de `AutoRun` (`HKLM`/`HKCU`) |
| `proxy.c` | Delegação transparente para `/c` e `stdin` redirecionado |
| `resolver.c` | Precedência `internos -> CWD -> %PATH% × %PATHEXT%` |
| `state_sync.c` | Serialização e aplicação atômica de `CWD` e ambiente UTF-16LE |
| `exec.c` | Executor híbrido + built-ins (`alias`, `unalias`, `theme`, `plumbus config`) |
| `console.c` | Modo raw/cooked do console Win32 e VT 24-bit |
| `history.c` | Histórico persistente em `%LocalAppData%\plumbus\history` e `Ctrl+R` |
| `completion.c` | Autocompletar de arquivos, `%VAR%` e subcomandos `git` |
| `highlight.c` | Syntax highlighting 24-bit em tempo real |
| `line_editor.c` | Editor de linha estilo Readline/Fish (`ReadConsoleInputW`) |
| `git_native.c` | Parser binário de repositório Git em C17 puro |
| `config.c` | Parser/gravador de `%USERPROFILE%\.cmdrc` e tabela de aliases |
| `prompt.c` | Renderizador dos temas `robbyrussell` e `agnoster` |
| `tui_config.c` | Configurador interativo em Alternate Screen Buffer (`F2` / `Ctrl+,`) |
