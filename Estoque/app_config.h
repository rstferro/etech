#pragma once
/* =====================================================================
 * app_config.h — Constantes, macros e configurações da aplicação.
 *
 * Requer raylib.h já incluído (para o tipo Color).
 * Inclua platform.h antes deste header.
 * ===================================================================== */

/* ------- Rede ------- */
#define WINDOWS_IP    "192.168.18.90"   /* informativo */
#define LINUX_IP      "192.168.18.104"  /* informativo */
#define NET_TCP_PORT   50506
#define NET_MAXLINE    2048

/* ------- Protocolo de mensagens ------- */
#define CMD_HELLO       "HELLO"
#define CMD_BYE         "BYE"
#define CMD_PING        "PING"
#define CMD_PONG        "PONG"
#define CMD_PULL_CSV    "PULL_CSV"
#define CMD_PUSH_CSV    "PUSH_CSV"
#define CMD_PULL_SALES  "PULL_SALES"
#define CMD_PUSH_SALES  "PUSH_SALES"
#define CMD_PRINT_SALE  "PRINT_SALE"
#define CMD_PRINT_QUOTE "PRINT_QUOTE"
#define CMD_ACK         "ACK"
#define CMD_ERR         "ERR"

/* ------- Limites e arquivos ------- */
#define MAX_ITEMS   2000
#define MAX_CART     512
#define CSV_FILE    "estoque.csv"
#define SALES_FILE  "vendas.csv"
#define QUOTE_FILE  "orcamentos.csv"

/*
 * Quantos dias antes do fim do mês a exportação automática é acionada.
 * Ex: 3 = exporta automaticamente nos últimos 3 dias do mês.
 */
#define EXPORT_DIAS_ANTES_FIM 1

/* ------- Paleta de cores (requer Color de raylib.h) ------- */
#define COL_BG       ((Color){ 18,  18,  18, 255})
#define COL_PANEL    ((Color){ 28,  28,  28, 255})
#define COL_HEADER   ((Color){ 36,  36,  36, 255})
#define COL_ROW_A    ((Color){ 32,  32,  32, 255})
#define COL_ROW_B    ((Color){ 26,  26,  26, 255})
#define COL_HILIGHT  ((Color){130,  88, 255,  64})
#define COL_TEXT     ((Color){240, 240, 240, 255})
#define COL_TEXT_DIM ((Color){180, 180, 180, 255})
#define COL_WARN     ((Color){230,  76,  76, 255})
