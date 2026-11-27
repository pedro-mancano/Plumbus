# Plumbus

**Drop-in replacement ultra-leve para o `cmd.exe` em C17 puro**, compilado estaticamente via CMake (~200 KB, zero dependências externas), com **100% de retrocompatibilidade** para scripts `.bat`/`.cmd` e uma experiência interativa moderna inspirada no **Oh My Bash**.

---

## Destaques

- **C17 Puro & Estático (~200 KB):** Sem C++, sem DLLs de runtime externas (`KERNEL32.dll` e `ADVAPI32.dll` apenas), Unicode nativo (`wchar_t` + Win32 `W` APIs).
- **Retrocompatibilidade Total:** Regras exatas de aspas (`/c`, `/k`, `/s`), proxy transparente em pipes/scripts, `AutoRun` (`HKLM`/`HKCU` com `/d`), e **Sync Bridge binário UTF-16LE** que persiste `CWD`, `PATH`, `SET` e `%ERRORLEVEL%` de scripts como `vcvarsall.bat`.
- **Editor de Linha Readline/Fish:** Atalhos Emacs (`Ctrl+A/E/W/U/K/L`, `Ctrl+Left/Right`), histórico persistente (`%LocalAppData%\plumbus\history`), busca reversa (`Ctrl+R`), *ghost text* inline, autocompletar (`Tab`/`Shift+Tab`) e *syntax highlighting* 24-bit em tempo real.
- **Parser Git Nativo em C (< 1 ms):** Lê `.git/HEAD`, `.git/index` (`DIRC` v2/v3/v4) e `.gitignore` diretamente — **nunca invoca `git.exe`**.
- **Configurador Visual TUI (`plumbus config` / `F2` / `Ctrl+,`):** Menu em *Alternate Screen Buffer* com Live Preview do prompt, alternância de tema (`robbyrussell` / `agnoster`), toggles de recursos, gerenciador de aliases e persistência em `%USERPROFILE%\.cmdrc`.

---

## Visual no Terminal

### Prompt `robbyrussell` (padrão) e `agnoster` (Powerline / Nerd Fonts)
```text
➜ ~\Projects\Plumbus git:(main) ✔ git status --short
                                    └────── ghost text ──────┘

 ~\Projects\Plumbus   main ✗  dir /a "Program Files"
```

### Configurador TUI (`plumbus config`, `F2` ou `Ctrl+,`)
```text
╭─────────────────────── Plumbus Configurator ───────────────────────╮
│ Live Preview:                                                      │
│ ➜ ~\Projects\Plumbus git:(main) ✔ git status --short               │
├────────────────────────────────────────────────────────────────────┤
│ ▶ Tema do Prompt                    robbyrussell                   │
│   Nerd Fonts (Powerline/Git)        ON                             │
│   Syntax Highlighting               ON                             │
│   Sugestões Inline (Ghost Text)     ON                             │
│   Status Git no Prompt              ON                             │
│   Gerenciar Aliases                 [2 configurado(s) -> Enter]    │
│                                                                    │
│ [S] Salvar no .cmdrc  |  [R] Restaurar Padrões  |  [Esc/Q] Cancelar│
╰────────────────────────────────────────────────────────────────────╯
```

---

## Instalação e Uso Seguro (Sem Alterar `System32`)

> **Importante:** Nunca substitua `C:\Windows\System32\cmd.exe`. O Plumbus foi projetado para rodar ao lado do `cmd.exe` nativo com total segurança.

1. Baixe `plumbus-v1.0.0-windows-x64.exe` (ou `plumbus-v1.0.0-windows-x64.zip`) na página de **Releases**.
2. Coloque o executável em uma pasta de sua preferência (ex.: `C:\Tools\plumbus\plumbus.exe`).
3. **Adicionar no Windows Terminal (Recomendado):**
   - Abra **Configurações** (`Ctrl+,`) -> **Adicionar um novo perfil** -> **Novo perfil vazio**.
   - **Nome:** `Plumbus`
   - **Linha de comando:** `C:\Tools\plumbus\plumbus.exe`
   - **Diretório inicial:** `%USERPROFILE%`
   - Para ícones Powerline no tema `agnoster`, selecione uma [Nerd Font](https://www.nerdfonts.com/) (ex.: *CaskaydiaCove Nerd Font* ou *JetBrainsMono Nerd Font*).
4. **Testar com `%ComSpec%` isolado (opcional):**
   ```cmd
   set ComSpec=C:\Tools\plumbus\plumbus.exe
   %ComSpec% /c echo Olá do Plumbus!
   ```
   O Plumbus detecta automaticamente loops recursivos por identidade física de arquivo (`GetFileInformationByHandle`) e jamais entra em fork-bomb.

---

## Atalhos de Teclado e Comandos Built-in

| Atalho | Ação |
|---|---|
| `F2` ou `Ctrl+,` | Abre o **Plumbus Configurator** preservando a linha atual |
| `Ctrl+A` / `Home` | Vai para o início da linha |
| `Ctrl+E` / `End` | Vai para o fim da linha (`End` / `→` no fim aceita *ghost text*) |
| `Ctrl+Left` / `Ctrl+Right` | Pula palavras |
| `Ctrl+W` | Apaga a palavra anterior |
| `Ctrl+U` / `Ctrl+K` | Apaga até o início / até o fim da linha |
| `Ctrl+L` | Limpa a tela |
| `Ctrl+R` | Busca incremental reversa no histórico (`Enter` executa, `Esc` sai) |
| `↑` / `↓` | Navega pelo histórico persistente |
| `Tab` / `Shift+Tab` | Autocompletar cíclico (arquivos, diretórios, `%VAR%`, subcomandos `git`) |
| `Ctrl+C` / `Ctrl+D` | Cancela linha atual / encerra em linha vazia |

### Comandos Built-in Interativos
- `plumbus config` / `config` — abre o configurador visual TUI
- `alias` / `alias nome=cmd` / `unalias nome` — gerencia aliases na sessão
- `theme [robbyrussell|agnoster]` — alterna o tema instantaneamente
- `cd` / `chdir`, `X:`, `pushd`, `popd`, `exit [N]` — fast-path nativo em C

Para detalhes completos do arquivo `%USERPROFILE%\.cmdrc`, consulte [`docs/configuration.md`](docs/configuration.md).

---

## Arquitetura e Segurança

- **Proxy Híbrido + Sync Bridge UTF-16LE:** Comandos `/c`, `/?` e entradas redirecionadas são delegados com fidelidade de handles e `ExitCode`. Na sessão interativa, comandos internos e scripts `.bat`/`.cmd` rodam via Sync Bridge (`/v:off` + `call ... ^%ERRORLEVEL^%`) sincronizando `CWD`, bloco completo de ambiente (incluindo `=X:`) e `%ERRORLEVEL%` em binário UTF-16LE.
- **Zero `git.exe`:** O status Git é calculado em C17 puro lendo `.git/HEAD`, `.git/index` e `.gitignore`.
- Mais detalhes em [`docs/architecture.md`](docs/architecture.md) e [`docs/phase0-cmd-analysis.md`](docs/phase0-cmd-analysis.md).

---

## Compilação via CMake

Requer **CMake >= 3.20** e **MSVC** (Visual Studio 2022 / Build Tools) ou **MinGW-w64 / Clang**:

```powershell
cmake -S . -B build_release -A x64 -DDEV_USE_LOCAL_BACKUP=OFF
cmake --build build_release --config Release --parallel
ctest --test-dir build_release -C Release --output-on-failure
```
