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
    MODAL_MONTH_SALES
} ModalMode;

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
