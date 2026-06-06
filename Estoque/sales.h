#pragma once
/* =====================================================================
 * sales.h — Operações de vendas, orçamento, carrinho e relatório mensal.
 * ===================================================================== */

#include "platform.h"
#include "app_types.h"

/* ------- Exportação mensal ------- */

/*
 * Exporta para "vendas_YYYY_MM.csv" todas as linhas de vendas.csv
 * que pertencem ao mês/ano indicados.
 *
 * forcar = 0 → pula se o arquivo de destino já existir (uso: auto-trigger).
 * forcar = 1 → sobrescreve sempre                      (uso: botão manual).
 *
 * Retorna:  1 = exportado com sucesso
 *           0 = já existia e forcar=0 (sem alteração)
 *          -1 = erro de I/O
 */
int ExportarVendasMes(int year, int month, int forcar);

/*
 * Verifica se estamos nos últimos EXPORT_DIAS_ANTES_FIM dias do mês
 * e, caso o arquivo ainda não exista, chama ExportarVendasMes(…, 0).
 * Deve ser chamada uma vez no startup da aplicação.
 *
 * Retorna o mesmo que ExportarVendasMes, ou 0 se não era fim de mês.
 */
int VerificarEExportarMesAtual(void);
void append_sales_csv(const char *seller, CartItem *cart, int cartCount, int discPct);
void sales_file_to_text(char **outBuf, int *outLen);
void merge_sales_text(const char *txt);

/* ------- Persistência de orçamentos ------- */
void append_quote_csv(const char *seller, CartItem *cart, int cartCount);

/* ------- Relatório mensal (desenha o modal raygui) ------- */
void DrawMonthlySalesModal(void);

/* ------- CRUD do carrinho de vendas ------- */
int  cartFindItem(int itemIndex);
void cartAdd(int itemIndex, int qty);
void cartRemoveAt(int pos);
void cartClear(void);

/* ------- CRUD do orçamento ------- */
int  quoteFindItem(int itemIndex);
void quoteAdd(int itemIndex, int qty);
void quoteRemoveAt(int pos);
void quoteClear(void);
