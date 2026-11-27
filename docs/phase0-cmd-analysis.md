# Fase 0 — Engenharia Reversa Segura e Mapeamento do `cmd.exe`

Este documento consolida os resultados empíricos obtidos exclusivamente contra a cópia isolada `./backup/cmd.exe` em ambiente sandbox (`build/phase0_probe/`), mantendo `C:\Windows\System32\cmd.exe`, `SysWOW64`, ACLs e Registro do Windows 100% intactos.

---

## 1. Ambiente, Toolchain e Verificação do Backup Local

### Backup Isolado (`./backup/cmd.exe`)
- **Origem (somente leitura):** `C:\Windows\System32\cmd.exe`
- **Destino isolado (ignorado no `.gitignore`):** `./backup/cmd.exe`
- **SHA-256 (Origem e Backup idênticos):** `15CD209C995B7D3009C91CA40892D5B8F1FB162F3E5F10716642EA278E8E0D7B`
- **Tamanho:** `339.968` bytes (`~332 KB`)
- **Versão:** `10.0.26100.9549 (WinBuild.160101.0800)`
- **Cabeçalho PE (`dumpbin /headers`):** Arquitetura `x64 (8664)`, Subsistema `3 (Windows CUI)`, `image version 10.00`.
- **Imports principais (`dumpbin /imports`):** `ntdll.dll`, API sets `api-ms-win-core-*` (console, file, process, registry, heap, synch, localization), `api-ms-win-crt-*`, `ext-ms-win-branding-winbrand-l1-1-0.dll`, `ext-ms-win-cmd-util-l1-1-0.dll`, `ext-ms-win-appmodel-shellexecute-l1-1-0.dll`, `Wldp.dll`.
- **Observação sobre MUI:** Ao executar `./backup/cmd.exe` isolado sem a pasta `pt-BR\cmd.exe.mui` adjacente, mensagens localizadas (`/?`, erros de sintaxe) exibem o fallback de ID de mensagem do sistema (`0x40002399`, etc.), enquanto toda a lógica de parsing, flags, execução e códigos de saída permanece 100% idêntica ao binário original.

### Toolchain Detectado
- **CMake:** `4.4.4` (`C:\Program Files\CMake\bin\cmake.exe`)
- **Compilador preferencial (MSVC):** Visual Studio 2022 Community — `MSVC 14.39.33519` (`Hostx64\x64\cl.exe`), com `dumpbin.exe` disponível.
- **Configuração de build alvo:** C17 puro, `/W4 /WX`, linkagem estática do CRT (`/MT`) para binário único `< 500 KB` sem DLLs externas de runtime.

---

## 2. Decisões Arquiteturais Confirmadas

1. **Nome do projeto / executável:** `plumbus.exe` (alvo CMake `plumbus`, histórico em `%LocalAppData%\plumbus\history`).
2. **Comportamento da flag `/k`:** **Híbrido (Opção B)** — delega integralmente ao backend `cmd.exe` se `stdin` estiver redirecionado (não-interativo); quando `stdin` for um console interativo, executa o comando do `/k` pela Ponte de Sincronização de Estado e em seguida entra no shell interativo do `plumbus.exe`.
3. **Ordem de resolução do backend:**
   1. Variável de ambiente `PLUMBUS_CMD_BACKEND` (override para testes isolados).
   2. `./backup/cmd.exe` quando compilado com `DEV_USE_LOCAL_BACKUP=ON` (desenvolvimento).
   3. Fallback de produção: `GetSystemDirectoryW()` + `\cmd.exe` (somente leitura/execução).
   - Sempre protegido por verificação anti-recursão via identidade física de arquivo (`GetFileInformationByHandle`).
4. **Toolchain:** Detecção automática no `CMakeLists.txt` dando preferência ao **MSVC** (ou MinGW-w64/Clang como fallback), sempre com CRT estático.

---

## 3. Regras Exatas de Parsing de Aspas (`/c`, `/k`, `/s`)

Todas as sondagens abaixo foram executadas via `CreateProcessW` com `lpCommandLine` bruto (evitando qualquer re-escaping de shell intermediário):

| Caso de Teste | `lpCommandLine` após `cmd.exe` | `ExitCode` | Saída Observada | Regra Aplicada |
|---|---|---:|---|---|
| `Q1_2quotes_echo` | `/d /c "echo hello"` | `0` | `hello` | 2 aspas; `echo hello` não é executável -> remove as 2 aspas |
| `Q2_2quotes_exe` | `/d /c "prog dir\hello.bat"` | `0` | `RAN_HELLO arg1=[]` | 2 aspas; caminho entre aspas resolve para executável/batch válido sem metacaracteres -> **preserva** as aspas |
| `Q3_slash_s_exe` | `/d /s /c "prog dir\hello.bat"` | `1` | Erro (`prog` não encontrado) | `/s` força remoção da primeira e última aspa antes do parse |
| `Q4_gt2_quotes` | `/d /c ""prog dir\hello.bat" "arg 1""` | `0` | `RAN_HELLO arg1=["arg 1"]` | `> 2` aspas -> remove a primeira e a última aspa |
| `Q5_2quotes_special` | `/d /c "echo a&echo b"` | `0` | `a\nb` | 2 aspas contendo caractere especial `&` -> remove as aspas |
| `Q6_tail_after_quote` | `/d /c "echo hi" & echo tail` | `0` | `hi \ntail` | 2 aspas seguidas de texto fora das aspas -> remove as aspas externas |
| `Q7_leading_spaces` | `/d /c   "echo leading"` | `0` | `leading` | Espaços entre `/c` e a primeira aspa são ignorados antes da regra de aspas |
| `Q8_glued_flags` | `/d/c"echo glued"` | `0` | `glued` | Flags sem espaço (`/d/c"..."`) são aceitas diretamente |

### Algoritmo Formal para `cmdline_strip_cmd_quotes`
Após localizar `/c` ou `/k` na linha bruta (`GetCommandLineW()`):
1. Avançar espaços em branco iniciais (`' '` e `'\t'`) logo após `/c` ou `/k`.
2. Se `/s` foi especificado: remover a primeira `"` (se presente no início) e a última `"` da string restante (se houver).
3. Se `/s` **não** foi especificado, verificar a condição de **preservação** do `cmd.exe`:
   - Deve haver **exatamente 2** caracteres `"` na string restante;
   - A string começa com `"` (após trim inicial) e termina com `"` (após trim final);
   - O trecho interno **não** contém nenhum caractere especial do shell: `& < > ( ) @ ^ |`;
   - O trecho interno contém ao menos um espaço em branco (`' '` ou `'\t'`);
   - O trecho interno resolve para um arquivo executável/batch existente (via `GetFileAttributesW` / busca em `PATH` + `PATHEXT`).
   - Se **todas** as condições acima forem verdadeiras -> **preservar** as aspas. Caso contrário, remover a primeira e a última `"`.

---

## 4. Referência Completa de Flags de Linha de Comando

Flags verificadas empiricamente (case-insensitive, podem aparecer coladas como `/d/q/c`):

| Flag | Comportamento Mapeado |
|---|---|
| `/c` | Executa a string de comando seguinte e encerra com o `ExitCode` do último comando. |
| `/k` | Executa a string de comando seguinte e permanece ativo (em `plumbus.exe`: delega ao backend se `stdin` redirecionado; executa via Sync Bridge + entra no REPL interativo se console). |
| `/s` | Modifica o tratamento da string após `/c` ou `/k`, forçando a remoção da primeira e última aspa. |
| `/q` | Desativa o eco de comandos (`ECHO OFF`). |
| `/d` | Suprime a execução dos comandos `AutoRun` do Registro (`HKLM` e `HKCU`). |
| `/a` | Saída de comandos internos em pipes/arquivos no formato ANSI/OEM (`41 0D 0A` observado para `echo A`). |
| `/u` | Saída de comandos internos em pipes/arquivos no formato UTF-16LE (`41 00 0D 00 0A 00` observado para `echo A`). |
| `/v:on` / `/v:off` | Habilita ou desabilita expansão atrasada (`!VAR!`). Padrão do sistema: `off` (salvo valor no Registro). |
| `/e:on` / `/e:off` | Habilita ou desabilita extensões de comando (`set /a`, `if defined`, `pushd`, etc.). Validado: `/e:off` faz `if defined PATH` falhar com `EC=1`. |
| `/f:on` / `/f:off` | Habilita ou desabilita caracteres de autocompletar de arquivo/diretório no console nativo. |
| `/t:fg` | Define cores de fundo (`f`) e primeiro plano (`g`) em hexadecimal (2 dígitos). |
| `/x` / `/y` | Flags legadas ignoradas / aliases históricos de `/e:on` e `/e:off`. |
| `/?` | Exibe ajuda de linha de comando e encerra. |

---

## 5. Ordem de Precedência de Execução

Validado empiricamente no sandbox `prec/`:
1. **Aliases internos** (camada interativa do shell — Fase 3).
2. **Comandos internos do `cmd.exe`** (`echo`, `cd`, `chdir`, `pushd`, `popd`, `set`, `dir`, `copy`, `move`, `del`, `erase`, `mklink`, `cls`, `path`, `for`, `if`, `call`, `setlocal`, `endlocal`, `type`, `ren`, `rename`, `md`, `mkdir`, `rd`, `rmdir`, `assoc`, `ftype`, `start`, `title`, `ver`, `vol`, `exit`, `rem`, `pause`, `prompt`, `color`, `date`, `time`, `verify`, `break`, `goto`, `shift`, etc.).
   - *Prova empírica:* `echo.bat` no CWD foi ignorado quando invocado `echo` -> executou o comando interno (`FROM_INTERNAL`).
3. **Diretório Atual (CWD)** iterando as extensões de `%PATHEXT%` na ordem (padrão `.COM;.EXE;.BAT;.CMD;...`).
   - *Prova empírica:* `prio.bat` no CWD venceu `prio.cmd` no CWD e `prio.bat` em `%PATH%` (`OUT=CWD_BAT`).
   - *Exceção:* Quando `NoDefaultCurrentDirectoryInExePath=1` está definido no ambiente, o CWD é pulado na busca sem caminho explícito (`OUT=PATH_BAT`).
4. **Diretórios de `%PATH%`** em ordem, testando para cada diretório as extensões de `%PATHEXT%` em ordem (se o comando não possuir extensão explícita).
5. **Troca de drive:** Comando no formato `X:` (apenas letra + `:`) troca o drive atual no shell.

---

## 6. Comportamento do Registro (`AutoRun`)

Leitura realizada em modo estritamente **read-only** (`Get-ItemProperty`):
- **`HKLM\Software\Microsoft\Command Processor`:**
  - Presente na máquina com: `CompletionChar=9`, `DefaultColor=0`, `EnableExtensions=1`, `InteractiveExtensionLevel=1`, `PathCompletionChar=9`, `LockBatchFilesWhenInUse=0`. Valor `AutoRun` ausente no momento.
- **`HKCU\Software\Microsoft\Command Processor`:**
  - Chave ausente na máquina no momento.
- **Regra do `cmd.exe`:** Quando `AutoRun` existe (`REG_SZ` ou `REG_EXPAND_SZ`), o `cmd.exe` executa `HKLM\...\AutoRun` primeiro e `HKCU\...\AutoRun` em seguida, antes de processar comandos interativos ou `/c`/`/k`, **exceto** quando `/d` é passado na linha de comando.

---

## 7. Estratégia Validada do Sync Bridge para Captura de `ERRORLEVEL`

O ajuste técnico solicitado sobre a expansão prematura de `%ERRORLEVEL%` em linha única de `cmd /c` foi testado empiricamente contra `./backup/cmd.exe`:

| Mecanismo Testado | Comando no `./backup/cmd.exe` | `ExitCode` Capturado | Preserva `!` literal (`/v:off`)? | Resultado |
|---|---|---:|---|---|
| `/v:on` + `!ERRORLEVEL!` | `/d /q /v:on /c "( echo a!PATH!b ) & echo EC=!ERRORLEVEL!"` | `0` | **Não** (expandiu `!PATH!`) | Rejeitado — altera semântica de scripts que contêm `!` |
| `/v:off` + `call %%ERRORLEVEL%%` | `/d /q /c "call ec_test.bat & call echo EC=%%ERRORLEVEL%%"` | `EC=%0%` (errado) | Sim | Rejeitado — `%ERRORLEVEL%` expande antes do comando |
| **`/v:off` + `call ^%ERRORLEVEL^%`** | `/d /q /c "( call ec_test.bat ) & call echo EC=^%ERRORLEVEL^%"` | **`EC=42`** | **Sim** (`a!PATH!b` intacto) | **Aprovado** |
| **`/v:off` + `call ^%ERRORLEVEL^%` (9009)** | `/d /q /c "( nonexistent_cmd_xyz ) & call echo EC=^%ERRORLEVEL^%"` | **`EC=9009`** | **Sim** | **Aprovado** |

### Template Comprovado para a Fase 1
- **Para comandos internos / compostos (com `/v:off` padrão):**
  `<backend_cmd> /d /q /c "( <comando_usuario> ) & "<self.exe>" --__sync-state "<arquivo_tmp>" ^%ERRORLEVEL^%"`
- **Para scripts `.bat`/`.cmd` invocados diretamente (ex.: `vcvarsall.bat x64`):**
  `<backend_cmd> /d /q /c "call <comando_usuario> & "<self.exe>" --__sync-state "<arquivo_tmp>" ^%ERRORLEVEL^%"`
  *(O `call` garante que `exit /b N` dentro do `.bat` retorne ao encadeamento e permita ao `--__sync-state` capturar `CWD`, bloco completo de ambiente UTF-16LE e `N` via `^%ERRORLEVEL^%`.)*
