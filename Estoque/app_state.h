#pragma once
/* =====================================================================
 * app_state.h — Declarações (extern) de todas as variáveis globais
 *               compartilhadas entre os módulos.
 *
 * As definições reais estão em app_state.c.
 * ===================================================================== */

#include "platform.h"
#include "app_types.h"

/* ------- Estoque ------- */
extern Item g_items[MAX_ITEMS];
extern int  g_count;

/* ------- Busca / Seleção / Scroll principal ------- */
extern char g_search[128];
extern bool g_searchEdit;
extern int  g_selected;
extern int  g_scrollIndex;
extern int  filtered_indices[MAX_ITEMS];
extern int  filtered_count;

/* ------- Modal ativo e formulário de edição ------- */
extern ModalMode g_modal;
extern Item      g_form;
extern bool      g_qtyEdit, g_editSku, g_editName, g_editLoc, g_editPrice, g_editCost;

/* ------- Carrinho de vendas ------- */
extern CartItem g_cart[MAX_CART];
extern int      g_cartCount;
extern char     g_seller[64];
extern int      cartSel, cartScroll;
extern char     cartStatus[256];
extern int      g_cartDiscPct;
extern bool     g_cartDiscEdit;

/* ------- Orçamento ------- */
extern CartItem g_quote[MAX_CART];
extern int      g_quoteCount;
extern int      quoteSel, quoteScroll;
extern char     quoteStatus[256];
extern int      g_quoteDiscPct;
extern bool     g_quoteDiscEdit;

/* ------- Modal "Adicionar ao carrinho" ------- */
extern int  addCart_itemIndex;
extern int  addCart_qty;
extern bool addCart_qtyEdit;

/* ------- Impressora ESC/POS ------- */
extern char          PRN_NAME[128];
extern int           PRN_WIDTH_CHARS;
extern unsigned char PRN_CODEPAGE;
extern bool          g_prnConnected;
extern double        g_prnLastCheck;
extern double        g_prnInterval;

/* PRN_SAFE_WIDTH depende de PRN_WIDTH_CHARS em tempo de execução */
#define PRN_SAFE_WIDTH (PRN_WIDTH_CHARS - 1)

/* ------- Status do peer (Linux) ------- */
extern bool g_linuxConnected;
extern char g_peerStatus[64];

/* ------- Travas de sincronização entre threads ------- */
extern CRITICAL_SECTION g_csvLock;
extern CRITICAL_SECTION g_sockLock;
extern CRITICAL_SECTION g_sendLock;

/* ------- Conexão TCP com o Linux ------- */
extern SOCKET            g_cliSock;
extern int               g_connected;
extern volatile uint32_t g_failStreak;
extern volatile int      g_needPush;
extern volatile int      g_salesNeedPush;

/* ------- Carimbo de sincronização do CSV ------- */
extern volatile long long g_csvStamp;
extern int                g_savingRemoteCsv;

/* ------- Estado do campo de texto avançado (ui_helpers) ------- */
extern char  *g_textActiveBuf;
extern int    g_textCursor;
extern int    g_textAnchor;
extern bool   g_textDragging;
extern int    g_textRepeatKey;
extern double g_textRepeatNext;
extern bool   forceFocusSearch;
extern bool   g_forceCursorEnd;

/* ------- Estado do campo de dinheiro (ui_helpers) ------- */
extern int   *g_moneyActivePtr;
extern bool   g_moneySelectedAll;
extern bool   g_moneyDragging;
extern float  g_moneyDragStartX;
extern int    g_moneyRepeatKey;
extern double g_moneyRepeatNext;

/* ------- Scroll do relatório mensal de vendas ------- */
extern int g_salesScroll;
