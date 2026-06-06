# Estoque de Componentes — Windows Server

Sistema de gestão de estoque com interface gráfica (Raylib + Raygui),
impressão ESC/POS e sincronização TCP com um peer Linux.

---

## Estrutura do projeto

```
estoque/
│
├── platform.h          # Includes de sistema na ordem correta
│                       # (Raylib → Raygui → Win32 → stdlib)
│
├── app_config.h        # Constantes, comandos de rede, cores, limites
├── app_types.h         # Tipos: Item, CartItem, ModalMode, SaleSummary
│
├── app_state.h         # Declarações extern de TODOS os globais
├── app_state.c         # Definições dos globais compartilhados
│
├── utils.h / .c        # Utilitários de string (trim, icase, split)
│
├── ui_helpers.h / .c   # Tema raygui, DrawTextInputBoxAdv,
│                       # DrawMoneyInputBox  ← define RAYGUI_IMPLEMENTATION
│
├── csv_io.h / .c       # Leitura/escrita CSV + CRUD de estoque
│
├── printer.h / .c      # Geração e envio de cupons ESC/POS (WinSpool)
│
├── sales.h / .c        # Vendas, orçamentos, carrinho, relatório mensal
│
├── network.h / .c      # Servidor TCP persistente + push/pull
│
├── app_windows.c       # main() + loop principal da UI  ← PONTO DE ENTRADA
│
└── Makefile            # Compilação com MinGW-w64
```

---

## Dependências

| Biblioteca | Versão testada | Onde obter |
|------------|---------------|-----------|
| **Raylib** | ≥ 5.0 | https://github.com/raysan5/raylib/releases |
| **Raygui** | ≥ 4.0 (single-header) | https://github.com/raysan5/raygui |
| **MinGW-w64** | ≥ 13 | https://www.mingw-w64.org/ |

Coloque `raylib.h`, `raygui.h` e `libraylib.a` no mesmo diretório
dos fontes (ou ajuste `RAYLIB_DIR` no `Makefile`).

---

## Compilar

```bat
REM Release
mingw32-make

REM Debug
mingw32-make debug

REM Limpar artefatos
mingw32-make clean
```

Ou manualmente com um único comando:

```bat
gcc app_state.c utils.c ui_helpers.c csv_io.c printer.c sales.c ^
    network.c app_windows.c -o estoque.exe ^
    -O2 -Wall -I. -mwindows ^
    -lraylib -lwinspool -lws2_32 -lopengl32 -lgdi32 -lwinmm
```

---

## Configurações rápidas (`app_state.c`)

| Variável | Padrão | Descrição |
|----------|--------|-----------|
| `PRN_NAME` | `"ELGIN i9(USB)"` | Nome da impressora no Windows |
| `PRN_WIDTH_CHARS` | `48` | Largura em colunas (80 mm=48, 58 mm=32) |
| `PRN_CODEPAGE` | `2` | Página de código ESC/POS (2=CP850, 3=CP860) |

Endereços de rede em `app_config.h`:

```c
#define WINDOWS_IP   "192.168.18.90"
#define LINUX_IP     "192.168.18.104"
#define NET_TCP_PORT  50506
```

---

## Regra de inclusão única do Raygui

`RAYGUI_IMPLEMENTATION` é definido **somente** em `ui_helpers.c`.
Todos os outros `.c` incluem `platform.h` normalmente (sem o define).

```
ui_helpers.c:
    #define RAYGUI_SUPPORT_ICONS 0
    #define RAYGUI_IMPLEMENTATION   ← única ocorrência
    #include "platform.h"

todos os outros .c:
    #include "platform.h"           ← apenas declarações
```

---

## Arquivos de dados gerados em runtime

| Arquivo | Conteúdo |
|---------|----------|
| `estoque.csv` | Itens do estoque |
| `vendas.csv` | Histórico de vendas |
| `orcamentos.csv` | Histórico de orçamentos |
| `last_print_payload.txt` | Último payload ESC/POS (debug) |
