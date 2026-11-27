# Guia de Configuração do Plumbus

## 1. Configurador Interativo (TUI)

Abra o configurador visual a qualquer momento sem perder o que você estava digitando na linha de comando:
- Pressionando **`F2`** ou **`Ctrl+,`**
- Executando **`plumbus config`** (ou **`config`** caso não exista um executável/script `config` no diretório atual)

### Controles do Configurador
- **`↑` / `↓`**: Navega entre as opções
- **`←` / `→` / `Espaço` / `Enter`**: Alterna o valor da opção (ou abre o Gerenciador de Aliases)
- **`S`**: Salva em `%USERPROFILE%\.cmdrc` e aplica imediatamente na sessão ativa
- **`R`**: Restaura todas as opções visuais para os padrões de fábrica
- **`Esc` / `Q`**: Cancela e descarta alterações não salvas

### Gerenciador de Aliases
Dentro de **Gerenciar Aliases**:
- **`A`**: Adiciona um novo alias no formato `nome=comando` (`Enter` confirma, `Esc` cancela)
- **`D` / `Delete`**: Remove o alias selecionado
- **`Esc`**: Volta ao menu principal

---

## 2. Arquivo `%USERPROFILE%\.cmdrc`

Arquivo texto UTF-8 carregado automaticamente ao iniciar uma sessão interativa.

### Diretivas Suportadas
| Diretiva | Valores | Padrão | Descrição |
|---|---|---|---|
| `theme` | `robbyrussell` \| `agnoster` | `robbyrussell` | Tema visual do prompt |
| `nerd_fonts` | `on` \| `off` | `on` | Habilita glifos Powerline (``, ``) no tema `agnoster` |
| `git_status` | `on` \| `off` | `on` | Exibe branch e status (`✔`/`✗`) no prompt |
| `syntax_highlighting` | `on` \| `off` | `on` | Colore comandos, flags, strings e operadores em tempo real |
| `ghost_text` | `on` \| `off` | `on` | Exibe sugestões inline baseadas no histórico |
| `alias` | `alias nome="comando"` | — | Define um alias expandido interativamente |
| `set` | `set VAR=VALOR` | — | Define variável de ambiente na inicialização |

---

## 3. Variáveis de Ambiente Especiais

| Variável | Uso |
|---|---|
| `PLUMBUS_CMD_BACKEND` | Sobrescreve o caminho do `cmd.exe` de backend (útil para testes isolados) |
| `PLUMBUS_RC_FILE` | Sobrescreve o caminho do `.cmdrc` |
| `PLUMBUS_HISTORY_FILE` | Sobrescreve o caminho do arquivo de histórico |
