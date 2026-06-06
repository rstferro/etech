#pragma once
/* =====================================================================
 * printer.h — Funções de impressão ESC/POS via USB (Windows/WinSpool).
 * ===================================================================== */

#include "platform.h"
#include "app_types.h"

/* Verifica periodicamente se a impressora está disponível */
void UpdatePrinterStatus(void);

/* Imprime cupom do carrinho atual em duas vias (não esvazia o carrinho) */
int PrintCurrentCartReceipt(const char *storeTitle);

/* Imprime cupom do orçamento atual */
int PrintCurrentQuoteReceipt(const char *storeTitle);

/*
 * Imprime cupom a partir de um payload de texto enviado pelo Linux.
 * Formato de cada linha do payload:
 *   SELLER: <nome>
 *   DISCOUNT: <pct>
 *   <sku>|<nome>|<qtd>|<preco>    (delimitadores aceitos: | ; , \t)
 *
 * is_quote = 0 → venda | 1 → orçamento
 * Retorna 0 em sucesso, código negativo em falha.
 */
int PrintPayloadReceiptDirect(const char *storeTitle, const char *payload, int is_quote);
