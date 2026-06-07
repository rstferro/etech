/* =====================================================================
 * app_state.c — Definições de todas as variáveis globais compartilhadas.
 * ===================================================================== */

#include "platform.h"
#include "app_types.h"
#include "app_state.h"

/* ------- Estoque ------- */
Item g_items[MAX_ITEMS];
int  g_count = 0;

/* ------- Busca / Seleção / Scroll principal ------- */
char g_search[128]   = {0};
bool g_searchEdit    = false;
int  g_selected      = -1;
int  g_scrollIndex   = 0;
int  filtered_indices[MAX_ITEMS];
int  filtered_count  = 0;

/* ------- Modal ativo e formulário de edição ------- */
ModalMode g_modal     = MODAL_NONE;
Item      g_form;
bool      g_qtyEdit   = false;
bool      g_editSku   = false;
bool      g_editName  = false;
bool      g_editLoc   = false;
bool      g_editPrice = false;
bool      g_editCost  = false;

/* ------- Carrinho de vendas ------- */
CartItem g_cart[MAX_CART];
int      g_cartCount    = 0;
char     g_seller[64]   = {0};
int      cartSel        = -1;
int      cartScroll     = 0;
char     cartStatus[256] = {0};
int      g_cartDiscPct  = 0;
bool     g_cartDiscEdit = false;

/* ------- Orçamento ------- */
CartItem g_quote[MAX_CART];
int      g_quoteCount    = 0;
int      quoteSel        = -1;
int      quoteScroll     = 0;
char     quoteStatus[256] = {0};
int      g_quoteDiscPct  = 0;
bool     g_quoteDiscEdit = false;

/* ------- Modal "Adicionar ao carrinho" ------- */
int  addCart_itemIndex = -1;
int  addCart_qty       = 1;
bool addCart_qtyEdit   = false;

/* ------- Impressora ESC/POS ------- */
char          PRN_NAME[128]   = "ELGIN i9(USB)"; /* ajuste conforme nome no Windows */
int           PRN_WIDTH_CHARS = 48;              /* 80mm: 48 | 58mm: 32 */
unsigned char PRN_CODEPAGE    = 2;               /* 2=CP850 | 3=CP860 */
bool          g_prnConnected  = false;
double        g_prnLastCheck  = 0.0;
double        g_prnInterval   = 3.0;

/* ------- Status do peer (Linux) ------- */
bool g_linuxConnected = false;
char g_peerStatus[64] = "Desconectado";

/* ------- Travas de sincronização entre threads ------- */
CRITICAL_SECTION g_csvLock;
CRITICAL_SECTION g_sockLock;
CRITICAL_SECTION g_sendLock;

/* ------- Conexão TCP com o Linux ------- */
SOCKET            g_cliSock       = INVALID_SOCKET;
int               g_connected     = 0;
volatile uint32_t g_failStreak    = 0;
volatile int      g_needPush      = 0;
volatile int      g_salesNeedPush = 0;

/* ------- Carimbo de sincronização do CSV ------- */
volatile long long g_csvStamp       = 0;
int                g_savingRemoteCsv = 0;

/* ------- Estado do campo de texto avançado ------- */
char  *g_textActiveBuf  = NULL;
int    g_textCursor     = 0;
int    g_textAnchor     = 0;
bool   g_textDragging   = false;
int    g_textRepeatKey  = 0;
double g_textRepeatNext = 0.0;
bool   forceFocusSearch = false;
bool   g_forceCursorEnd = false;

/* ------- Estado do campo de dinheiro ------- */
int   *g_moneyActivePtr   = NULL;
bool   g_moneySelectedAll = false;
bool   g_moneyDragging    = false;
float  g_moneyDragStartX  = 0.0f;
int    g_moneyRepeatKey   = 0;
double g_moneyRepeatNext  = 0.0;

/* ------- Ordenação da tabela principal ------- */
int  g_sortCol = SORT_COL_NONE;
bool g_sortAsc = true;

/* ------- Scroll do relatório mensal de vendas ------- */
int g_salesScroll = 0;
