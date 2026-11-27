# Guia de Configuração do Plumbus

## Configurador Visual TUI
Você pode configurar o Plumbus interativamente sem editar arquivos manualmente:
- Digite `plumbus config` (ou `config` se não houver executável `config` na pasta atual)
- Ou pressione **`F2`** / **`Ctrl+,`** a qualquer momento durante a digitação (o texto já digitado na linha é preservado).

### Controles no Configurador
- `↑` / `↓` — navega entre as opções
- `←` / `→`, `Espaço` ou `Enter` — alterna valor ou abre o gerenciador de aliases
- `[S]` — salva em `%USERPROFILE%\.cmdrc` e aplica imediatamente
- `[R]` — restaura as configurações padrão
- `[Esc]` / `[Q]` — cancela sem alterar o `.cmdrc`

No sub-painel **Gerenciar Aliases**:
- `[A]` — adiciona um novo alias no formato `nome=comando`
- `[D]` / `Delete` — remove o alias selecionado
- `[Esc]` — volta ao menu principal

---

## Arquivo `%USERPROFILE%\.cmdrc`

Arquivo texto em UTF-8 carregado automaticamente na inicialização interativa:

```ini
# Plumbus Configuration (~/.cmdrc)

# Tema do prompt: robbyrussell | agnoster
theme=robbyrussell

# Ícones Powerline / Nerd Font: on | off
nerd_fonts=on

# Indicador de branch e dirty/clean no prompt: on | off
git_status=on

# Colorização em tempo real na linha de comando: on | off
syntax_highlighting=on

# Sugestões inline baseadas no histórico: on | off
ghost_text=on

# Variáveis de ambiente iniciais
set EDITOR=code

# Aliases persistentes
alias ll="dir /a"
alias gs="git status"
alias gp="git pull"
```

---

## Variáveis de Ambiente de Controle

| Variável | Uso |
|---|---|
| `PLUMBUS_CMD_BACKEND` | Sobrescreve o caminho do `cmd.exe` de backend (útil para testes isolados) |
| `PLUMBUS_RC_FILE` | Sobrescreve o caminho do arquivo `.cmdrc` |
| `PLUMBUS_HISTORY_FILE` | Sobrescreve o caminho do arquivo de histórico |
