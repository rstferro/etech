#pragma once
/* =====================================================================
 * app_types.h — Definições de tipos da aplicação.
 * ===================================================================== */

#include "app_config.h"

/* ------- Item de estoque ------- */
typedef struct {
    char  sku[64];
    char  name[128];
    char  location[64];
    int   qty;
    float price;
    float cost_price;
} Item;

/* ------- Entrada no carrinho / orçamento ------- */
typedef struct {
    int itemIndex;
    int qty;
} CartItem;

/* ------- Modo do modal ativo ------- */
typedef enum {
    MODAL_NONE,
    MODAL_ADD,
    MODAL_EDIT,
    MODAL_CONFIRM_DELETE,
    MODAL_CART,
    MODAL_ADD_CHOOSE,
    MODAL_QUOTE,
    MODAL_MONTH_SALES,
    MODAL_AUDIT_LOG
} ModalMode;

/* ------- Coluna de ordenação da tabela principal ------- */
typedef enum {
    SORT_COL_NONE = 0,
    SORT_COL_SKU,
    SORT_COL_NAME,
    SORT_COL_LOCATION,
    SORT_COL_QTY,
    SORT_COL_PRICE,
    SORT_COL_COST
} SortColumn;

/* ------- Linha agregada do relatório mensal ------- */
#define MAX_SALES_SUMMARY MAX_ITEMS

typedef struct {
    char  sku[64];
    char  name[128];
    int   qty;
    float totalCost;
    float totalSale;
    float totalDiscount;
    float profit;
} SaleSummary;
