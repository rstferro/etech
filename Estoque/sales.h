#pragma once
/* =====================================================================
 * sales.h — Operações de vendas, orçamento, carrinho e relatório mensal.
 * ===================================================================== */

#include "platform.h"
#include "app_types.h"

/* ------- Persistência de vendas ------- */
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
