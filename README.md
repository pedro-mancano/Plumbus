# Plumbus

**Drop-in replacement ultra-leve para o `cmd.exe` do Windows**, escrito estritamente em **C17 puro** (zero C++, zero dependências externas), com **~205 KB** em binário estático único, **100% de retrocompatibilidade** com scripts Batch (`.bat`/`.cmd`) e experiência interativa moderna inspirada no **Oh My Bash** e **Fish Shell**.

---

## Destaques

- **100% Retrocompatível:** Suporta flags (`/c`, `/k`, `/s`, `/q`, `/d`, `/a`, `/u`, `/v:on|off`, `/e:on|off`, `/t:fg`, `/?`), regras exatas de aspas do `cmd.exe`, pipes, redirecionamento, `AutoRun` e scripts como `vcvarsall.bat` (persistindo `CWD`, `PATH`, variáveis `SET` e `ERRORLEVEL`).
- **TUI & Line Editor (Readline/Fish):** Atalhos estilo Emacs (`Ctrl+A/E/W/U/K/L`, `Ctrl+Left/Right`), busca reversa incremental (`Ctrl+R`), *ghost text* inline, autocompletar contextual (`Tab`) e *syntax highlighting* 24-bit em tempo real.
- **Parser Git Nativo (< 1 ms):** Lê `.git/HEAD`, `.git/index` (`DIRC` v2/v3/v4) e `.gitignore` diretamente em C17 — **nunca** invoca `git.exe` em subprocesso.
- **Configurador Visual Integrado:** Pressione **`F2`**, **`Ctrl+,`** ou digite **`plumbus config`** para abrir o menu TUI com *Live Preview*.
- **Zero Dependências:** Linkagem estática (`/MT`), importando apenas `KERNEL32.dll` e `ADVAPI32.dll`.

---

## Prévia Visual

### Temas de Prompt (`robbyrussell`, `agnoster`, `omb-box`, `pure`, `dracula`, `minimal`) + Syntax Highlighting

```text
# Tema padrão (robbyrussell) com repositório Git limpo e comando válido:
➜ ~\Projects\Plumbus git:(main) ✔ git status --short
                                   ^^^ ^^^^^^ ^^^^^^^
                                   verde ciano ghost-text

# Tema Oh My Bash duas linhas (omb-box) com parser Git nativo:
┏━━pedro at WORKSTATION in ~\Projects\Plumbus on main ✔
┗━$ git status

# Tema Powerline (agnoster com Nerd Fonts):
 ~\Projects\Plumbus   main ✔  dir /a "minha pasta"
```

### Configurador Interativo (`plumbus config` / `F2` / `Ctrl+,`)

```text
╭──────────────────────── Plumbus Configurator ────────────────────────╮
│ Live Preview:                                                        │
│ ➜ ~\Projects\Plumbus git:(main) ✔ git status --short                 │
├──────────────────────────────────────────────────────────────────────┤
│ ▶ Tema do Prompt                   robbyrussell                      │
│   Nerd Fonts (Powerline/Git)       ON                                │
│   Syntax Highlighting              ON                                │
│   Sugestões Inline (Ghost Text)    ON                                │
│   Status Git no Prompt             ON                                │
│   Gerenciar Aliases                [2 configurado(s) -> Enter]       │
│                                                                      │
│ Use ↑/↓ para navegar | ←/→, Espaço ou Enter para alternar            │
├──────────────────────────────────────────────────────────────────────┤
│ [S] Salvar no .cmdrc  |  [R] Restaurar Padrões  |  [Esc/Q] Cancelar  │
╰──────────────────────────────────────────────────────────────────────╯
```

---

## Instalação e Uso Seguro (Sem Alterar o `System32`)

> **Segurança:** Nunca substitua `C:\Windows\System32\cmd.exe`. O `plumbus.exe` foi projetado para rodar de forma isolada e delegar scripts/internos ao `cmd.exe` nativo em modo somente leitura, com proteção anti-recursão por identidade física de arquivo.

1. Baixe `plumbus-v1.0.0-windows-x64.exe` (ou extraia `plumbus-v1.0.0-windows-x64.zip`) da página de **Releases** para uma pasta local (ex.: `C:\Tools\Plumbus\plumbus.exe`).
2. **Adicionar ao Windows Terminal (Recomendado):**
   - Abra o **Windows Terminal** -> **Configurações** (`Ctrl+,`) -> **Adicionar um novo perfil** -> **Novo perfil vazio**.
   - **Nome:** `Plumbus`
   - **Linha de comando:** `C:\Tools\Plumbus\plumbus.exe`
   - **Diretório inicial:** `%USERPROFILE%`
   - *(Opcional para `agnoster`)* Selecione uma fonte **Nerd Font** (ex.: *CaskaydiaCove Nerd Font* ou *JetBrainsMono Nerd Font*).
3. **Testar via `%ComSpec%` em sessão isolada:**
   ```bat
   set ComSpec=C:\Tools\Plumbus\plumbus.exe
   %ComSpec% /c echo Olá do Plumbus!
   ```

---

## Atalhos de Teclado e Comandos

### Atalhos do Editor de Linha
| Atalho | Ação |
|---|---|
| `F2` / `Ctrl+,` | Abre o **Plumbus Configurator** preservando a linha digitada |
| `Ctrl+A` / `Home` | Vai para o início da linha |
| `Ctrl+E` / `End` | Vai para o fim da linha (`End` no fim aceita *ghost text*) |
| `Ctrl+Left` / `Ctrl+Right` | Pula uma palavra para trás / frente |
| `Ctrl+W` | Apaga a palavra anterior |
| `Ctrl+U` / `Ctrl+K` | Apaga até o início / fim da linha |
| `Ctrl+L` | Limpa a tela mantendo o prompt |
| `Ctrl+R` | Busca incremental reversa no histórico (`Enter` executa, `Esc` sai) |
| `↑` / `↓` | Navega no histórico (`%LocalAppData%\plumbus\history`) |
| `→` (no fim da linha) | Aceita sugestão *ghost text* |
| `Tab` / `Shift+Tab` | Autocompletar cíclico (arquivos, pastas, `%VAR%`, subcomandos `git`) |
| `Ctrl+C` / `Ctrl+D` | Cancela linha atual / encerra em linha vazia |

### Comandos Built-in Interativos
- `plumbus config` / `config` — abre o configurador TUI (se não houver `config.exe`/`.bat` no diretório atual)
- `alias` / `alias nome=comando` / `unalias nome` — gerencia aliases em tempo real
- `theme` / `theme <nome>` — alterna entre os 6 temas (`robbyrussell`, `agnoster`, `omb-box`, `pure`, `dracula`, `minimal`)
- `cd`, `chdir`, `X:`, `pushd`, `popd`, `exit [N]` — fast-path nativo em C

### Arquivo de Configuração (`%USERPROFILE%\.cmdrc`)
Veja a referência completa em [`docs/configuration.md`](docs/configuration.md):
```ini
theme=robbyrussell
nerd_fonts=on
git_status=on
syntax_highlighting=on
ghost_text=on
alias ll="dir /a"
alias gs="git status"
```

---

## Arquitetura e Build com CMake

Documentação técnica detalhada em [`docs/architecture.md`](docs/architecture.md) e [`docs/phase0-cmd-analysis.md`](docs/phase0-cmd-analysis.md).

### Requisitos de Build
- **CMake >= 3.20**
- **MSVC** (Visual Studio 2022 / Build Tools), **MinGW-w64** ou **Clang** no Windows

```powershell
# Build de Produção (usa System32\cmd.exe read-only como backend)
cmake -S . -B build_release -A x64 -DDEV_USE_LOCAL_BACKUP=OFF
cmake --build build_release --config Release --parallel
ctest --test-dir build_release -C Release --output-on-failure
```
