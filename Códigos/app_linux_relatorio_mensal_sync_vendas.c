// app_linux.c (CLIENTE Linux com IPs fixos)
// Estoque de Componentes — C + raylib + raygui (tema roxo/cinza/branco/preto)
// P2P manual: Linux conecta no Windows (IP fixo) via TCP 50506
// - Pull periódico (5s) e Ping (2s), com reconexão automática
// - Push imediato em QUALQUER modificação local (add/edit/del/sale/quote/save)
// - NÃO faz PULL enquanto g_dirty!=0 (evita sobrescrever alterações locais)
// - ACEITA PUSH_CSV assíncrono vindo do Windows a qualquer momento
//   **AJUSTE**: QUANDO g_dirty!=0, o PUSH_CSV recebido é LIDO e DESCARTADO (não aplicado)
// - Print proxy: Linux envia PAYLOAD do cupom/orçamento e o Windows imprime
// - Modo ORÇAMENTO (imprime via Windows, não baixa estoque)
// - Modal do botão [+] (Carrinho/Orçamento)
// - Desconto (%) em Carrinho e Orçamento
// - Subtotais estáveis (não variam com o scroll)
//
// Compilar (Linux/Zorin):
// gcc app_linux.c -o estoque -O2 -Wall -I. \
//   -lraylib -lGL -lm -lpthread -ldl -lrt -lX11
// # se pedir: acrescente -lXrandr -lXi -lXinerama
//
// CSVs:
// estoque.csv -> sku,name,location,qty,price,cost_price
// vendas.csv  -> gerado ao finalizar a venda (baixa estoque)
// orcamentos.csv -> gerado ao finalizar orçamento (não baixa estoque)

#include "raylib.h"
#define RAYGUI_IMPLEMENTATION
#define RAYGUI_SUPPORT_ICONS 0
#include "raygui.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <time.h>
#include <pthread.h>

// ======= PARAMETROS DE REDE (IPs FIXOS) =======
#define NET_TCP_PORT 50506
#define NET_ROLE_NAME "LINUX"
#define NET_MAXLINE 2048

// Defina os IPs fixos (ajuste aqui se mudar)
#define IP_WINDOWS "192.168.18.90"
#define IP_LINUX   "192.168.18.104" // usado só para debug/status

// Comandos do protocolo
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

// ======= APP =======
#define MAX_ITEMS 2000
#define MAX_CART  512
#define CSV_FILE   "estoque.csv"
#define SALES_FILE "vendas.csv"
#define QUOTE_FILE "orcamentos.csv"

// -------------------- Cores do Tema --------------------
static const Color COL_BG      = { 18, 18, 18, 255};
static const Color COL_PANEL   = { 28, 28, 28, 255};
static const Color COL_HEADER  = { 36, 36, 36, 255};
static const Color COL_ROW_A   = { 32, 32, 32, 255};
static const Color COL_ROW_B   = { 26, 26, 26, 255};
static const Color COL_HILIGHT = {130, 88, 255, 64}; // roxo translúcido
static const Color COL_TEXT    = {240, 240, 240, 255};
static const Color COL_TEXT_DIM= {180, 180, 180, 255};
static const Color COL_WARN    = {230, 76, 76, 255};

static void DrawTextClippedCell(const char *text, Rectangle bounds, int fontSize, Color color){
    if(!text) text = "";
    if(bounds.width <= 0 || bounds.height <= 0) return;
    BeginScissorMode((int)bounds.x, (int)bounds.y, (int)bounds.width, (int)bounds.height);
    DrawText(text, (int)bounds.x, (int)bounds.y, fontSize, color);
    EndScissorMode();
}

static void ApplyTheme(void){
    GuiSetStyle(DEFAULT, TEXT_SIZE, 18);
    GuiSetStyle(DEFAULT, TEXT_SPACING, 1);
    GuiSetStyle(DEFAULT, BACKGROUND_COLOR,      0x121212FF);
    GuiSetStyle(DEFAULT, BORDER_COLOR_NORMAL,   0x7A3DF5FF);
    GuiSetStyle(DEFAULT, BASE_COLOR_NORMAL,     0x7A3DF5FF);
    GuiSetStyle(DEFAULT, TEXT_COLOR_NORMAL,     0xFFFFFFFF);
    GuiSetStyle(DEFAULT, BORDER_COLOR_FOCUSED,  0x9C6CFFFF);
    GuiSetStyle(DEFAULT, BASE_COLOR_FOCUSED,    0x9C6CFFFF);
    GuiSetStyle(DEFAULT, TEXT_COLOR_FOCUSED,    0xFFFFFFFF);
    GuiSetStyle(DEFAULT, BORDER_COLOR_PRESSED,  0x5E2DC2FF);
    GuiSetStyle(DEFAULT, BASE_COLOR_PRESSED,    0x5E2DC2FF);
    GuiSetStyle(DEFAULT, TEXT_COLOR_PRESSED,    0xFFFFFFFF);
    GuiSetStyle(DEFAULT, BORDER_COLOR_DISABLED, 0x555555FF);
    GuiSetStyle(DEFAULT, BASE_COLOR_DISABLED,   0x444444FF);
    GuiSetStyle(DEFAULT, TEXT_COLOR_DISABLED,   0xAAAAAAFF);
    GuiSetStyle(DEFAULT, LINE_COLOR,            0x3C3C3CFF);
}



// -------------------- Campos de texto com seleção --------------------
// Suporta: clique, seleção arrastando o mouse, Ctrl+A, Backspace/Delete segurados,
// setas/Home/End e digitação UTF-8 básica.
static char *g_textActiveBuf = NULL;
static int g_textCursor = 0;
static int g_textAnchor = 0;
static bool g_textDragging = false;
static int g_textRepeatKey = 0;
static double g_textRepeatNext = 0.0;

static int clampi_local(int v, int lo, int hi){ if(v<lo) return lo; if(v>hi) return hi; return v; }
static int utf8_prev_index(const char *s, int idx){
    if(idx <= 0) return 0;
    idx--;
    while(idx > 0 && (((unsigned char)s[idx] & 0xC0) == 0x80)) idx--;
    return idx;
}
static int utf8_next_index(const char *s, int idx){
    int len = (int)strlen(s);
    if(idx >= len) return len;
    unsigned char c = (unsigned char)s[idx];
    int step = 1;
    if((c & 0x80) == 0x00) step = 1;
    else if((c & 0xE0) == 0xC0) step = 2;
    else if((c & 0xF0) == 0xE0) step = 3;
    else if((c & 0xF8) == 0xF0) step = 4;
    return clampi_local(idx + step, 0, len);
}
static int text_width_prefix(const char *s, int bytes, int fontSize){
    char tmp[1024];
    if(bytes < 0) bytes = 0;
    if(bytes > (int)sizeof(tmp)-1) bytes = (int)sizeof(tmp)-1;
    memcpy(tmp, s, (size_t)bytes); tmp[bytes] = 0;
    return MeasureText(tmp, fontSize);
}
static int text_index_from_mouse_x(const char *s, int mouseX, int textX, int fontSize){
    int len = (int)strlen(s);
    if(mouseX <= textX) return 0;
    int best = len;
    for(int i=0; i<len; ){
        int ni = utf8_next_index(s, i);
        int mid = textX + text_width_prefix(s, ni, fontSize) - (text_width_prefix(s, ni, fontSize) - text_width_prefix(s, i, fontSize))/2;
        if(mouseX < mid){ best = i; break; }
        i = ni;
    }
    return best;
}
static bool repeat_key_ready_text(int key){
    double now = GetTime();
    if(IsKeyPressed(key)){
        g_textRepeatKey = key;
        g_textRepeatNext = now + 0.38;
        return true;
    }
    if(IsKeyDown(key) && g_textRepeatKey == key && now >= g_textRepeatNext){
        g_textRepeatNext = now + 0.045;
        return true;
    }
    if(g_textRepeatKey == key && !IsKeyDown(key)) g_textRepeatKey = 0;
    return false;
}
static void text_delete_selection(char *text){
    int a = g_textCursor < g_textAnchor ? g_textCursor : g_textAnchor;
    int b = g_textCursor > g_textAnchor ? g_textCursor : g_textAnchor;
    if(a != b){
        int len = (int)strlen(text);
        a = clampi_local(a,0,len); b = clampi_local(b,0,len);
        memmove(text + a, text + b, strlen(text + b) + 1);
        g_textCursor = g_textAnchor = a;
    }
}
static void text_insert_utf8(char *text, int cap, const char *ins, int insBytes){
    if(!ins || insBytes <= 0) return;
    text_delete_selection(text);
    int len = (int)strlen(text);
    if(len + insBytes >= cap) return;
    memmove(text + g_textCursor + insBytes, text + g_textCursor, (size_t)(len - g_textCursor + 1));
    memcpy(text + g_textCursor, ins, (size_t)insBytes);
    g_textCursor += insBytes;
    g_textAnchor = g_textCursor;
}
static bool DrawTextInputBoxAdv(Rectangle bounds, char *text, int cap, bool *editMode){
    bool clickedFocus = false;
    int fontSize = GuiGetStyle(DEFAULT, TEXT_SIZE);
    if(fontSize <= 0) fontSize = 18;
    int len = (int)strlen(text);
    if(*editMode && g_textActiveBuf != text){
        g_textActiveBuf = text;
        g_textCursor = len;
        g_textAnchor = len;
        g_textDragging = false;
    }
    if(g_textActiveBuf == text){
        g_textCursor = clampi_local(g_textCursor, 0, len);
        g_textAnchor = clampi_local(g_textAnchor, 0, len);
    }

    Vector2 mp = GetMousePosition();
    bool hover = CheckCollisionPointRec(mp, bounds);
    int textX = (int)bounds.x + 10;
    int textY = (int)(bounds.y + (bounds.height - fontSize)/2.0f + 1);

    if(IsMouseButtonPressed(MOUSE_LEFT_BUTTON)){
        if(hover){
            *editMode = true; clickedFocus = true;
            if(g_textActiveBuf != text){
                g_textActiveBuf = text;
                g_textCursor = (int)strlen(text);
                g_textAnchor = g_textCursor;
            }
            g_textCursor = text_index_from_mouse_x(text, (int)mp.x, textX, fontSize);
            g_textAnchor = g_textCursor;
            g_textDragging = true;
        } else if(g_textActiveBuf == text){
            *editMode = false;
            g_textDragging = false;
        }
    }
    if(g_textActiveBuf == text && g_textDragging && IsMouseButtonDown(MOUSE_LEFT_BUTTON)){
        g_textCursor = text_index_from_mouse_x(text, (int)mp.x, textX, fontSize);
    }
    if(IsMouseButtonReleased(MOUSE_LEFT_BUTTON) && g_textActiveBuf == text) g_textDragging = false;

    bool active = (*editMode && g_textActiveBuf == text);
    if(active){
        bool ctrl = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);
        bool shift = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
        if(ctrl && IsKeyPressed(KEY_A)){ g_textAnchor = 0; g_textCursor = (int)strlen(text); }

        if(repeat_key_ready_text(KEY_BACKSPACE)){
            if(g_textCursor != g_textAnchor) text_delete_selection(text);
            else if(g_textCursor > 0){
                int p = utf8_prev_index(text, g_textCursor);
                memmove(text + p, text + g_textCursor, strlen(text + g_textCursor) + 1);
                g_textCursor = g_textAnchor = p;
            }
        }
        if(repeat_key_ready_text(KEY_DELETE)){
            if(g_textCursor != g_textAnchor) text_delete_selection(text);
            else {
                int n = utf8_next_index(text, g_textCursor);
                if(n != g_textCursor) memmove(text + g_textCursor, text + n, strlen(text + n) + 1);
            }
        }
        if(IsKeyPressed(KEY_LEFT)){
            if(!shift && g_textCursor != g_textAnchor){
                g_textCursor = (g_textCursor < g_textAnchor) ? g_textCursor : g_textAnchor;
                g_textAnchor = g_textCursor;
            } else {
                int old = g_textCursor;
                g_textCursor = utf8_prev_index(text, g_textCursor);
                if(!shift) g_textAnchor = g_textCursor; else if(g_textAnchor == old) g_textAnchor = old;
            }
        }
        if(IsKeyPressed(KEY_RIGHT)){
            if(!shift && g_textCursor != g_textAnchor){
                g_textCursor = (g_textCursor > g_textAnchor) ? g_textCursor : g_textAnchor;
                g_textAnchor = g_textCursor;
            } else {
                int old = g_textCursor;
                g_textCursor = utf8_next_index(text, g_textCursor);
                if(!shift) g_textAnchor = g_textCursor; else if(g_textAnchor == old) g_textAnchor = old;
            }
        }
        if(IsKeyPressed(KEY_HOME)){ if(!shift) g_textAnchor = 0; g_textCursor = 0; }
        if(IsKeyPressed(KEY_END)){ g_textCursor = (int)strlen(text); if(!shift) g_textAnchor = g_textCursor; }

        int ch = GetCharPressed();
        while(ch > 0){
            if(ch >= 32){
                int bytes = 0;
                const char *utf8 = CodepointToUTF8(ch, &bytes);
                if(utf8 && bytes > 0) text_insert_utf8(text, cap, utf8, bytes);
            }
            ch = GetCharPressed();
        }
    }

    Color fill = active ? (Color){42,34,62,255} : (Color){32,32,32,255};
    Color border = active ? (Color){200,200,205,255} : (Color){122,61,245,255};
    DrawRectangleRec(bounds, fill);
    DrawRectangleLinesEx(bounds, active ? 3.0f : 1.0f, border);

    BeginScissorMode((int)bounds.x+4, (int)bounds.y+2, (int)bounds.width-8, (int)bounds.height-4);
    int a = g_textCursor < g_textAnchor ? g_textCursor : g_textAnchor;
    int b = g_textCursor > g_textAnchor ? g_textCursor : g_textAnchor;
    if(active && a != b){
        int sx = textX + text_width_prefix(text, a, fontSize);
        int ex = textX + text_width_prefix(text, b, fontSize);
        DrawRectangle(sx, (int)bounds.y + 6, ex - sx, (int)bounds.height - 12, (Color){150,150,155,180});
    }
    DrawText(text, textX, textY, fontSize, COL_TEXT);
    if(active && a != b){
        char sel[1024]; int n = b-a; if(n > (int)sizeof(sel)-1) n = (int)sizeof(sel)-1;
        memcpy(sel, text+a, (size_t)n); sel[n]=0;
        int sx = textX + text_width_prefix(text, a, fontSize);
        DrawText(sel, sx, textY, fontSize, (Color){20,20,20,255});
    }
    if(active){
        int cx = textX + text_width_prefix(text, g_textCursor, fontSize) + 1;
        DrawRectangle(cx, (int)bounds.y + 6, 3, (int)bounds.height - 12, (Color){185,185,190,255});
        DrawRectangle(cx+4, (int)bounds.y + 8, 1, (int)bounds.height - 16, (Color){235,235,235,255});
    }
    EndScissorMode();
    return clickedFocus;
}

// -------------------- Campo de dinheiro em centavos --------------------
// Digitação estilo maquininha: 0.00 -> 0.01 -> 0.12 -> 1.23 -> 12.34
static int *g_moneyActivePtr = NULL;
static bool g_moneySelectedAll = false;
static bool g_moneyDragging = false;
static float g_moneyDragStartX = 0.0f;
static int g_moneyRepeatKey = 0;
static double g_moneyRepeatNext = 0.0;

static bool repeat_key_ready_money(int key){
    double now = GetTime();
    if(IsKeyPressed(key)){
        g_moneyRepeatKey = key;
        g_moneyRepeatNext = now + 0.38;
        return true;
    }
    if(IsKeyDown(key) && g_moneyRepeatKey == key && now >= g_moneyRepeatNext){
        g_moneyRepeatNext = now + 0.045;
        return true;
    }
    if(g_moneyRepeatKey == key && !IsKeyDown(key)) g_moneyRepeatKey = 0;
    return false;
}
static int money_to_cents(float v){
    if(v < 0.0f) v = 0.0f;
    return (int)(v*100.0f + 0.5f);
}
static void cents_to_money_text(char *out, size_t outsz, int cents){
    if(cents < 0) cents = 0;
    snprintf(out, outsz, "%d.%02d", cents/100, cents%100);
}
static void DrawMoneyInputBox(Rectangle bounds, int *cents, bool editMode){
    Vector2 mp = GetMousePosition();
    bool hover = CheckCollisionPointRec(mp, bounds);
    if(editMode && g_moneyActivePtr != cents){
        g_moneyActivePtr = cents;
        g_moneySelectedAll = false;
    }
    if(IsMouseButtonPressed(MOUSE_LEFT_BUTTON) && hover){
        g_moneyActivePtr = cents;
        g_moneySelectedAll = false;
        g_moneyDragging = true;
        g_moneyDragStartX = mp.x;
    }
    if(g_moneyActivePtr == cents && g_moneyDragging && IsMouseButtonDown(MOUSE_LEFT_BUTTON)){
        if(mp.x > g_moneyDragStartX + 5.0f || mp.x < g_moneyDragStartX - 5.0f || !hover) g_moneySelectedAll = true;
    }
    if(IsMouseButtonReleased(MOUSE_LEFT_BUTTON) && g_moneyActivePtr == cents) g_moneyDragging = false;

    bool active = editMode && (g_moneyActivePtr == cents);
    if(active){
        bool ctrl = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);
        if(ctrl && IsKeyPressed(KEY_A)) g_moneySelectedAll = true;

        int ch = GetCharPressed();
        while(ch > 0){
            if(ch >= '0' && ch <= '9'){
                if(g_moneySelectedAll){ *cents = 0; g_moneySelectedAll = false; }
                if(*cents < 999999999) *cents = (*cents)*10 + (ch - '0');
            }
            ch = GetCharPressed();
        }
        if(repeat_key_ready_money(KEY_BACKSPACE)){
            if(g_moneySelectedAll){ *cents = 0; g_moneySelectedAll = false; }
            else *cents = (*cents)/10;
        }
        if(repeat_key_ready_money(KEY_DELETE)){
            *cents = 0;
            g_moneySelectedAll = false;
        }
        if(*cents < 0) *cents = 0;
    }

    char txt[64];
    cents_to_money_text(txt, sizeof(txt), *cents);

    Color fill = active ? (Color){42,34,62,255} : (Color){32,32,32,255};
    Color border = active ? (Color){200,200,205,255} : (Color){122,61,245,255};

    DrawRectangleRec(bounds, fill);
    DrawRectangleLinesEx(bounds, active ? 3.0f : 1.0f, border);

    int fontSize = GuiGetStyle(DEFAULT, TEXT_SIZE);
    if(fontSize <= 0) fontSize = 18;

    int textX = (int)bounds.x + 10;
    int textY = (int)(bounds.y + (bounds.height - fontSize)/2.0f + 1);
    int textW = MeasureText(txt, fontSize);
    if(active && g_moneySelectedAll){
        DrawRectangle(textX-2, (int)bounds.y + 6, textW+6, (int)bounds.height - 12, (Color){150,150,155,180});
        DrawText(txt, textX, textY, fontSize, (Color){20,20,20,255});
    } else {
        DrawText(txt, textX, textY, fontSize, COL_TEXT);
    }

    if(active && !g_moneySelectedAll){
        int cx = textX + textW + 5;
        if(cx > (int)(bounds.x + bounds.width - 8)) cx = (int)(bounds.x + bounds.width - 8);
        DrawRectangle(cx, (int)bounds.y + 6, 3, (int)bounds.height - 12, (Color){185,185,190,255});
        DrawRectangle(cx+4, (int)bounds.y + 8, 1, (int)bounds.height - 16, (Color){235,235,235,255});
    }
}

// -------------------- Tipos --------------------
typedef struct {
    char  sku[64];
    char  name[128];
    char  location[64];
    int   qty;
    float price;
    float cost_price;
} Item;

typedef struct {
    int itemIndex; // índice no array g_items
    int qty;
} CartItem;

typedef enum {
    MODAL_NONE, MODAL_ADD, MODAL_EDIT, MODAL_CONFIRM_DELETE,
    MODAL_CART, MODAL_ADD_CHOOSE, MODAL_QUOTE, MODAL_MONTH_SALES
} ModalMode;

// -------------------- Estado global --------------------
static Item g_items[MAX_ITEMS];
static int  g_count = 0;

static char g_search[128] = {0};
static bool g_searchEdit  = false;
static int  g_selected    = -1;
static int  g_scrollIndex = 0;
static int  filtered_indices[MAX_ITEMS];
static int  filtered_count = 0;

static ModalMode g_modal = MODAL_NONE;
static Item g_form;
static bool g_qtyEdit=false, g_editSku=false, g_editName=false, g_editLoc=false, g_editPrice=false, g_editCost=false;

// carrinho (venda)
static CartItem g_cart[MAX_CART];
static int  g_cartCount = 0;
static char g_seller[64] = {0};
static int  cartSel = -1, cartScroll = 0;
static char cartStatus[256] = {0};
static int  g_cartDiscPct = 0;
static bool g_cartDiscEdit = false;

// orçamento
static CartItem g_quote[MAX_CART];
static int  g_quoteCount = 0;
static int  quoteSel = -1, quoteScroll = 0;
static char quoteStatus[256] = {0};
static int  g_quoteDiscPct = 0;
static bool g_quoteDiscEdit = false;

// modal "adicionar" pelo [+]
static int  addCart_itemIndex = -1;
static int  addCart_qty = 1;
static bool addCart_qtyEdit = false;

// flag de sync (PUSH imediato)
static volatile int g_dirty = 0;
static volatile int g_salesDirty = 0;

// Carimbo de sincronização: impede que o Linux aceite um CSV antigo
// depois de uma alteração local (evita produto apagado voltar).
static volatile long long g_csvStamp = 0;
static long long make_sync_stamp(void){
    long long st = (long long)time(NULL)*1000LL + ((long long)clock()*1000LL/CLOCKS_PER_SEC)%1000LL;
    if(st <= 0) st = 1;
    return st;
}
static void touch_csv_stamp(void){
    long long st = make_sync_stamp();
    if(st <= g_csvStamp) st = g_csvStamp + 1;
    g_csvStamp = st;
}

// Trava global do estoque: a UI e a thread de rede acessam g_items/g_count ao mesmo tempo.
// Sem isso, o Linux podia enviar um CSV parcialmente atualizado e criar itens fantasmas.
static pthread_mutex_t g_csvLock = PTHREAD_MUTEX_INITIALIZER;

// -------------------- Impressora (Linux via proxy Windows) --------------------
static bool   g_prnConnected = false; // true quando socket TCP ok (proxy)
static double g_prnLastCheck = 0.0;
static double g_prnInterval  = 1.0;

// -------------------- Helpers gerais --------------------
static void trim(char *s){
    int i=(int)strlen(s)-1;
    while(i>=0 && (s[i]==' '||s[i]=='\n'||s[i]=='\r'||s[i]=='\t')){ s[i]=0; i--; }
    char *p=s; while(*p==' '||*p=='\t') p++;
    if(p!=s) memmove(s,p,strlen(s)+1);
}
static int starts_with_icase(const char *s, const char *p){
    while (*p && *s){
        unsigned char a = (unsigned char)*s++;
        unsigned char b = (unsigned char)*p++;
        if (tolower(a) != tolower(b)) return 0;
    }
    return *p == '\0';
}
static int contains_icase(const char *h, const char *n){
    if (!*n) return 1;
    size_t nl = strlen(n);
    for (const char *p = h; *p; p++){
        size_t i = 0;
        while (i < nl && p[i] && tolower((unsigned char)p[i]) == tolower((unsigned char)n[i])) i++;
        if (i == nl) return 1;
    }
    return 0;
}

// -------------------- CSV --------------------
static void to_csv_line(char *out, size_t outsz, const Item *it){
    // ATENÇÃO: evite vírgulas em 'name' para não quebrar CSV simples
    snprintf(out,outsz,"%s,%s,%s,%d,%.2f,%.2f\n",
             it->sku,it->name,it->location,it->qty,it->price,it->cost_price);
}
static int from_csv_line(const char *line, Item *it){
    char buf[2048];
    strncpy(buf,line,sizeof(buf)-1); buf[sizeof(buf)-1]=0;
    char *tok[8]={0}; int nt=0;
    for(char *p=strtok(buf,","); p && nt<8; p=strtok(NULL,",")) tok[nt++]=p;
    if(nt<4) return 0;
    strncpy(it->sku, tok[0]?tok[0]:"", sizeof(it->sku)-1);
    strncpy(it->name,tok[1]?tok[1]:"", sizeof(it->name)-1);
    strncpy(it->location,tok[2]?tok[2]:"", sizeof(it->location)-1);
    it->sku[sizeof(it->sku)-1]=0; it->name[sizeof(it->name)-1]=0; it->location[sizeof(it->location)-1]=0;
    trim(it->sku); trim(it->name); trim(it->location);
    it->qty   = atoi(tok[3]?tok[3]:"0");
    it->price = (nt>=5 && tok[4]) ? (float)strtod(tok[4], NULL) : 0.0f;
    it->cost_price = (nt>=6 && tok[5]) ? (float)strtod(tok[5], NULL) : 0.0f;
    return 1;
}
static int save_csv(const char *path){
    touch_csv_stamp();
    pthread_mutex_lock(&g_csvLock);
    FILE *f=fopen(path,"wb");
    if(!f){
        pthread_mutex_unlock(&g_csvLock);
        return 0;
    }
    fputs("sku,name,location,qty,price,cost_price\n",f);
    char line[1024];
    for(int i=0;i<g_count;i++){
        to_csv_line(line,sizeof(line),&g_items[i]);
        fputs(line,f);
    }
    fclose(f);
    pthread_mutex_unlock(&g_csvLock);

    g_dirty = 1; // marcou alteração local -> push
    return 1;
}
static int load_csv(const char *path){
    pthread_mutex_lock(&g_csvLock);
    FILE *f=fopen(path,"rb");
    if(!f){
        pthread_mutex_unlock(&g_csvLock);
        return 0;
    }
    g_count=0; char line[2048];
    if(!fgets(line,sizeof(line),f)){
        fclose(f);
        pthread_mutex_unlock(&g_csvLock);
        return 0;
    }
    while(fgets(line,sizeof(line),f) && g_count<MAX_ITEMS){
        Item it; if(from_csv_line(line,&it)) g_items[g_count++]=it;
    }
    fclose(f);
    pthread_mutex_unlock(&g_csvLock);
    return 1;
}
static void append_sales_csv(const char *seller, CartItem *cart, int cartCount, int discPct){
    FILE *f=fopen(SALES_FILE,"ab"); if(!f) return;
    fseek(f,0,SEEK_END); long sz=ftell(f);
    if(sz==0){
        const char *hdr="datetime,seller,sku,name,qty,unit_cost,unit_price,line_cost,line_total,discount_pct,line_discount,line_net_total,line_profit\n";
        fwrite(hdr,1,strlen(hdr),f);
    }
    if(discPct < 0) discPct = 0;
    if(discPct > 100) discPct = 100;
    time_t t=time(NULL); struct tm *tmv=localtime(&t); char dt[32];
    strftime(dt,sizeof(dt),"%Y-%m-%d %H:%M:%S",tmv);
    for(int i=0;i<cartCount;i++){
        Item *it=&g_items[cart[i].itemIndex];
        float lineCost = it->cost_price * (float)cart[i].qty;
        float lineTotal = it->price * (float)cart[i].qty;
        float lineDiscount = lineTotal * ((float)discPct/100.0f);
        float lineNet = lineTotal - lineDiscount; if(lineNet < 0.0f) lineNet = 0.0f;
        float lineProfit = lineNet - lineCost;
        char line[2048];
        snprintf(line,sizeof(line),"%s,%s,%s,%s,%d,%.2f,%.2f,%.2f,%.2f,%d,%.2f,%.2f,%.2f\n",
                 dt,seller,it->sku,it->name,cart[i].qty,it->cost_price,it->price,lineCost,lineTotal,discPct,lineDiscount,lineNet,lineProfit);
        fwrite(line,1,strlen(line),f);
    }
    fclose(f);
    g_salesDirty = 1;
}
static void append_quote_csv(const char *seller, CartItem *cart, int cartCount){
    FILE *f=fopen(QUOTE_FILE,"ab"); if(!f) return;
    fseek(f,0,SEEK_END); long sz=ftell(f);
    if(sz==0){ const char *hdr="datetime,seller,sku,name,qty,unit_price,line_total\n"; fwrite(hdr,1,strlen(hdr),f); }
    time_t t=time(NULL); struct tm *tmv=localtime(&t); char dt[32];
    strftime(dt,sizeof(dt),"%Y-%m-%d %H:%M:%S",tmv);
    for(int i=0;i<cartCount;i++){
        Item *it=&g_items[cart[i].itemIndex];
        float lineTotal = it->price * (float)cart[i].qty;
        char line[2048];
        snprintf(line,sizeof(line),"%s,%s,%s,%s,%d,%.2f,%.2f\n",
                 dt,seller,it->sku,it->name,cart[i].qty,it->price,lineTotal);
        fwrite(line,1,strlen(line),f);
    }
    fclose(f);
}


// -------------------- Relatório de vendas do mês --------------------
#define MAX_SALES_SUMMARY MAX_ITEMS

typedef struct {
    char sku[64];
    char name[128];
    int qty;
    float totalCost;
    float totalSale;
    float totalDiscount;
    float profit;
} SaleSummary;

static int g_salesScroll = 0;

static int is_leap_year(int year){
    return ((year % 4 == 0 && year % 100 != 0) || (year % 400 == 0));
}
static int days_in_month(int year, int month){
    static const int days[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    if(month == 2) return days[1] + is_leap_year(year);
    if(month < 1 || month > 12) return 30;
    return days[month-1];
}
static void current_date_parts(int *year, int *month, int *day){
    time_t t = time(NULL);
#ifdef _WIN32
    struct tm tmv;
    localtime_s(&tmv, &t);
    if(year) *year = tmv.tm_year + 1900;
    if(month) *month = tmv.tm_mon + 1;
    if(day) *day = tmv.tm_mday;
#else
    struct tm *tmv = localtime(&t);
    if(year) *year = tmv ? tmv->tm_year + 1900 : 1970;
    if(month) *month = tmv ? tmv->tm_mon + 1 : 1;
    if(day) *day = tmv ? tmv->tm_mday : 1;
#endif
}
static int split_csv_cols(char *line, char **cols, int maxCols){
    int n = 0;
    char *p = line;
    while(p && n < maxCols){
        cols[n++] = p;
        char *comma = strchr(p, ',');
        if(!comma) break;
        *comma = 0;
        p = comma + 1;
    }
    return n;
}
static int find_item_by_sku(const char *sku){
    for(int i=0;i<g_count;i++){
        if(strcmp(g_items[i].sku, sku) == 0) return i;
    }
    return -1;
}
static int find_sale_summary(SaleSummary *rows, int count, const char *sku, const char *name){
    for(int i=0;i<count;i++){
        if(strcmp(rows[i].sku, sku) == 0 && strcmp(rows[i].name, name) == 0) return i;
    }
    return -1;
}
static void load_month_sales(SaleSummary *rows, int *rowCount, float *totalCost, float *totalSale, float *totalDiscount, float *totalProfit, int *year, int *month, int *dim){
    int cy=0, cm=0, cd=0;
    current_date_parts(&cy, &cm, &cd);
    (void)cd;
    int mdays = days_in_month(cy, cm);
    if(year) *year = cy;
    if(month) *month = cm;
    if(dim) *dim = mdays;

    *rowCount = 0;
    *totalCost = 0.0f;
    *totalSale = 0.0f;
    *totalDiscount = 0.0f;
    *totalProfit = 0.0f;

    FILE *f = fopen(SALES_FILE, "rb");
    if(!f) return;

    char line[4096];
    if(!fgets(line, sizeof(line), f)){
        fclose(f);
        return;
    }

    while(fgets(line, sizeof(line), f)){
        line[strcspn(line, "\r\n")] = 0;
        if(!line[0]) continue;

        char buf[4096];
        strncpy(buf, line, sizeof(buf)-1);
        buf[sizeof(buf)-1] = 0;

        char *cols[16] = {0};
        int nt = split_csv_cols(buf, cols, 16);
        if(nt < 7) continue;

        int sy=0, sm=0, sd=0;
        if(sscanf(cols[0], "%d-%d-%d", &sy, &sm, &sd) != 3) continue;
        if(sy != cy || sm != cm || sd < 1 || sd > mdays) continue;

        const char *sku = cols[2] ? cols[2] : "";
        const char *name = cols[3] ? cols[3] : "";
        int qty = atoi(cols[4] ? cols[4] : "0");
        if(qty <= 0) continue;

        float lineCost = 0.0f;
        float lineGross = 0.0f;
        float lineDiscount = 0.0f;
        float lineNet = 0.0f;
        float lineProfit = 0.0f;

        if(nt >= 13){
            // Formato novo:
            // datetime,seller,sku,name,qty,unit_cost,unit_price,line_cost,line_total,discount_pct,line_discount,line_net_total,line_profit
            lineCost = (float)strtod(cols[7] ? cols[7] : "0", NULL);
            lineGross = (float)strtod(cols[8] ? cols[8] : "0", NULL);
            lineDiscount = (float)strtod(cols[10] ? cols[10] : "0", NULL);
            lineNet = (float)strtod(cols[11] ? cols[11] : "0", NULL);
            lineProfit = (float)strtod(cols[12] ? cols[12] : "0", NULL);
            (void)lineGross;
        } else {
            // Formato antigo:
            // datetime,seller,sku,name,qty,unit_price,line_total
            float unitPrice = (float)strtod(cols[5] ? cols[5] : "0", NULL);
            lineGross = (float)strtod(cols[6] ? cols[6] : "0", NULL);
            if(lineGross <= 0.0f) lineGross = unitPrice * (float)qty;
            lineNet = lineGross;
            int itemIdx = find_item_by_sku(sku);
            float unitCost = (itemIdx >= 0) ? g_items[itemIdx].cost_price : 0.0f;
            lineCost = unitCost * (float)qty;
            lineProfit = lineNet - lineCost;
        }

        int pos = find_sale_summary(rows, *rowCount, sku, name);
        if(pos < 0 && *rowCount < MAX_SALES_SUMMARY){
            pos = (*rowCount)++;
            memset(&rows[pos], 0, sizeof(rows[pos]));
            strncpy(rows[pos].sku, sku, sizeof(rows[pos].sku)-1);
            strncpy(rows[pos].name, name, sizeof(rows[pos].name)-1);
        }
        if(pos >= 0){
            rows[pos].qty += qty;
            rows[pos].totalCost += lineCost;
            rows[pos].totalSale += lineNet;
            rows[pos].totalDiscount += lineDiscount;
            rows[pos].profit += lineProfit;
        }

        *totalCost += lineCost;
        *totalSale += lineNet;
        *totalDiscount += lineDiscount;
        *totalProfit += lineProfit;
    }

    fclose(f);
}
static void DrawMonthlySalesModal(void){
    SaleSummary rows[MAX_SALES_SUMMARY];
    int rowCount = 0, year = 0, month = 0, dim = 0;
    float totalCost = 0.0f, totalSale = 0.0f, totalDiscount = 0.0f, totalProfit = 0.0f;

    load_month_sales(rows, &rowCount, &totalCost, &totalSale, &totalDiscount, &totalProfit, &year, &month, &dim);

    Rectangle panel = {GetScreenWidth()/2.0f-560, GetScreenHeight()/2.0f-310, 1120, 620};
    DrawRectangleRec(panel, COL_PANEL);
    DrawRectangleLinesEx(panel, 2, (Color){122,61,245,255});

    DrawText(TextFormat("Vendas do mês atual - %02d/%04d (1 a %d)", month, year, dim), panel.x+16, panel.y+12, 24, COL_TEXT);
    DrawText(TextFormat("Custo: R$ %.2f    Venda líquida: R$ %.2f    Descontos: R$ %.2f    Lucro: R$ %.2f",
                        totalCost, totalSale, totalDiscount, totalProfit),
             panel.x+16, panel.y+46, 22, (totalProfit >= 0.0f) ? COL_TEXT : COL_WARN);

    Rectangle header = {panel.x+16, panel.y+84, panel.width-32, 32};
    DrawRectangleRec(header, COL_HEADER);

    int xSku = (int)header.x + 8;
    int xName = (int)header.x + 145;
    int xQty = (int)header.x + 545;
    int xCost = (int)header.x + 625;
    int xSale = (int)header.x + 765;
    int xProfit = (int)header.x + 920;

    DrawText("SKU", xSku, header.y+7, 18, COL_TEXT);
    DrawText("Produto", xName, header.y+7, 18, COL_TEXT);
    DrawText("Qtd", xQty, header.y+7, 18, COL_TEXT);
    DrawText("Custo total", xCost, header.y+7, 18, COL_TEXT);
    DrawText("Venda total", xSale, header.y+7, 18, COL_TEXT);
    DrawText("Lucro", xProfit, header.y+7, 18, COL_TEXT);

    Rectangle list = {panel.x+16, panel.y+116, panel.width-32, 420};
    DrawRectangleRec(list, (Color){24,24,24,255});

    int rowH = 30;
    int visible = (int)(list.height / rowH);
    if(g_salesScroll < 0) g_salesScroll = 0;
    if(g_salesScroll > rowCount-visible) g_salesScroll = rowCount-visible;
    if(g_salesScroll < 0) g_salesScroll = 0;

    int wheel = GetMouseWheelMove();
    if(CheckCollisionPointRec(GetMousePosition(), list) && wheel != 0){
        g_salesScroll -= wheel;
        if(g_salesScroll < 0) g_salesScroll = 0;
        if(g_salesScroll > rowCount-visible) g_salesScroll = rowCount-visible;
        if(g_salesScroll < 0) g_salesScroll = 0;
    }

    for(int r=0;r<visible;r++){
        int idx = g_salesScroll + r;
        if(idx >= rowCount) break;
        int y = (int)(list.y + r*rowH);
        Rectangle row = {list.x, (float)y, list.width, (float)rowH};
        DrawRectangleRec(row, (r%2==0) ? COL_ROW_A : COL_ROW_B);

        DrawTextClippedCell(rows[idx].sku, (Rectangle){(float)xSku, (float)y+7, 125, 22}, 18, COL_TEXT);
        DrawTextClippedCell(rows[idx].name, (Rectangle){(float)xName, (float)y+7, 385, 22}, 18, COL_TEXT);
        DrawText(TextFormat("%d", rows[idx].qty), xQty, y+7, 18, COL_TEXT);
        DrawText(TextFormat("R$ %.2f", rows[idx].totalCost), xCost, y+7, 18, COL_TEXT);
        DrawText(TextFormat("R$ %.2f", rows[idx].totalSale), xSale, y+7, 18, COL_TEXT);
        DrawText(TextFormat("R$ %.2f", rows[idx].profit), xProfit, y+7, 18, rows[idx].profit >= 0.0f ? COL_TEXT : COL_WARN);
    }

    if(rowCount == 0){
        DrawText("Nenhuma venda registrada neste mês.", panel.x+32, panel.y+140, 22, COL_TEXT_DIM);
    } else {
        DrawText(TextFormat("Produtos diferentes vendidos: %d", rowCount), panel.x+16, panel.y+548, 20, COL_TEXT_DIM);
    }

    if(GuiButton((Rectangle){panel.x+panel.width-160, panel.y+panel.height-52, 130, 36}, "Fechar")){
        g_modal = MODAL_NONE;
        g_searchEdit = false;
    }
}

// -------------------- CRUD + Filtro --------------------
static void rebuild_filter(void){
    filtered_count=0;
    for(int i=0;i<g_count;i++){
        if(!g_search[0] ||
           starts_with_icase(g_items[i].sku,g_search) ||
           contains_icase(g_items[i].name,g_search) ||
           contains_icase(g_items[i].location,g_search))
        {
            filtered_indices[filtered_count++]=i;
        }
    }
    if(g_selected>=filtered_count) g_selected=filtered_count-1;
    if(filtered_count<=0) g_scrollIndex=0;
}
static void add_item(const Item *it){
    pthread_mutex_lock(&g_csvLock);
    if(g_count<MAX_ITEMS){
        g_items[g_count++]=*it;
        g_dirty=1;
    }
    pthread_mutex_unlock(&g_csvLock);
}
static void update_item(int idx,const Item *it){
    pthread_mutex_lock(&g_csvLock);
    if(idx>=0 && idx<g_count){
        g_items[idx]=*it;
        g_dirty=1;
    }
    pthread_mutex_unlock(&g_csvLock);
}
static void delete_item(int idx){
    pthread_mutex_lock(&g_csvLock);
    if(idx>=0 && idx<g_count){
        for(int i=idx;i<g_count-1;i++) g_items[i]=g_items[i+1];
        g_count--;
        g_selected=-1;
        g_dirty=1;
    }
    pthread_mutex_unlock(&g_csvLock);
}

// -------------------- Carrinho/Orçamento ops --------------------
static int cartFindItem(int itemIndex){ for(int i=0;i<g_cartCount;i++) if(g_cart[i].itemIndex==itemIndex) return i; return -1; }
static void cartAdd(int itemIndex, int qty){
    if(qty<=0) return;
    int pos = cartFindItem(itemIndex);
    if(pos>=0) g_cart[pos].qty += qty;
    else if(g_cartCount<MAX_CART){ g_cart[g_cartCount].itemIndex=itemIndex; g_cart[g_cartCount].qty=qty; g_cartCount++; }
}
static void cartRemoveAt(int pos){
    if(pos<0||pos>=g_cartCount) return;
    for(int i=pos;i<g_cartCount-1;i++) g_cart[i]=g_cart[i+1];
    g_cartCount--; if(cartSel>=g_cartCount) cartSel=g_cartCount-1;
}
static void cartClear(void){ g_cartCount=0; cartSel=-1; }

static int quoteFindItem(int itemIndex){ for(int i=0;i<g_quoteCount;i++) if(g_quote[i].itemIndex==itemIndex) return i; return -1; }
static void quoteAdd(int itemIndex, int qty){
    if(qty<=0) return;
    int pos = quoteFindItem(itemIndex);
    if(pos>=0) g_quote[pos].qty += qty;
    else if(g_quoteCount<MAX_CART){ g_quote[g_quoteCount].itemIndex=itemIndex; g_quote[g_quoteCount].qty=qty; g_quoteCount++; }
}
static void quoteRemoveAt(int pos){
    if(pos<0||pos>=g_quoteCount) return;
    for(int i=pos;i<g_quoteCount-1;i++) g_quote[i]=g_quote[i+1];
    g_quoteCount--; if(quoteSel>=g_quoteCount) quoteSel=g_quoteCount-1;
}
static void quoteClear(void){ g_quoteCount=0; quoteSel=-1; }

// ===================================================
// =============== REDE / SYNC (Linux client) ========
// ===================================================
#ifndef _WIN32
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include <errno.h>
typedef int SOCKET;
#define INVALID_SOCKET (-1)

static SOCKET g_sock = INVALID_SOCKET;
static int    g_connected = 0;
static pthread_mutex_t g_sockLock = PTHREAD_MUTEX_INITIALIZER;

static void msleep(int ms){
    struct timespec ts={ms/1000,(ms%1000)*1000000L}; nanosleep(&ts,NULL);
}
static int tcp_connect_blocking(const char *ip, int port){
    SOCKET s = socket(AF_INET, SOCK_STREAM, 0);
    if(s==INVALID_SOCKET) return -1;
    struct sockaddr_in r={0};
    r.sin_family=AF_INET; r.sin_port=htons(port);
    if(inet_pton(AF_INET, ip, &r.sin_addr)<=0){ close(s); return -2; }

    struct timeval tv; tv.tv_sec=5; tv.tv_usec=0;
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    if(connect(s,(struct sockaddr*)&r,sizeof(r))<0){ close(s); return -3; }

    const char *hello = CMD_HELLO "\n" NET_ROLE_NAME "\n.\n";
    if(send(s, hello, (int)strlen(hello), 0)<=0){ close(s); return -4; }
    return s;
}
static void net_close_locked(void){
    if(g_sock!=INVALID_SOCKET){
        const char *bye = CMD_BYE "\n.\n"; send(g_sock, bye, (int)strlen(bye), 0);
        close(g_sock); g_sock = INVALID_SOCKET;
    }
    g_connected = 0;
}
static int send_all_locked(const char *buf, int len){
    int sent=0;
    while(sent<len){
        int n = send(g_sock, buf+sent, len-sent, 0);
        if(n<0){
            if(errno==EINTR) continue;
            if(errno==EWOULDBLOCK || errno==EAGAIN){ msleep(5); continue; }
            return -1;
        }
        if(n==0) return -1;
        sent += n;
    }
    return 0;
}
static int send_cmd_payload_locked(const char *cmd, const char *payload){
    char head[256];
    int n = snprintf(head,sizeof(head),"%s\n", cmd);
    if(n<=0) return -1;
    if(send_all_locked(head, n)<0) return -1;
    if(payload && *payload){
        if(send_all_locked(payload, (int)strlen(payload))<0) return -1;
    }
    if(send_all_locked("\n.\n", 3)<0) return -1;
    return 0;
}
static int recv_line_locked(char *buf, int max){
    int i=0; char c;
    while(i<max-1){
        int n=recv(g_sock,&c,1,0);
        if(n<0){
            if(errno==EINTR) continue;
            if(errno==EWOULDBLOCK || errno==EAGAIN){ msleep(5); continue; }
            return -1;
        }
        if(n==0) return -1;
        if(c=='\n'){ buf[i]=0; return i; }
        buf[i++]=c;
    }
    buf[i]=0; return i;
}
static int recv_payload_until_dot_locked(char *buf, int max){
    int total=0;
    for(;;){
        char line[NET_MAXLINE];
        int n = recv_line_locked(line,sizeof(line));
        if(n<0) return -1;
        if(strcmp(line,".")==0){
            if(total<max) buf[total]=0;
            return total;
        }
        int need = n + 1; // recoloca '\n'
        if(total+need >= max) return -1;
        memcpy(buf+total,line,n); buf[total+n]='\n';
        total += need;
    }
}

// ---- CSV (thread-safe) ----
static void csv_to_text_locked(char **outBuf, int *outLen){
    pthread_mutex_lock(&g_csvLock);
    size_t cap = 1024 + g_count*128;
    char *txt = (char*)malloc(cap);
    size_t pos=0;
    pos += snprintf(txt+pos, cap-pos, "#STAMP:%lld\nsku,name,location,qty,price,cost_price\n", (long long)g_csvStamp);
    for(int i=0;i<g_count;i++){
        char line[1024]; to_csv_line(line,sizeof(line),&g_items[i]);
        size_t L=strlen(line);
        if(pos+L+8>=cap){ cap*=2; txt=(char*)realloc(txt,cap); }
        memcpy(txt+pos,line,L); pos+=L;
    }
    pthread_mutex_unlock(&g_csvLock);
    *outBuf=txt; *outLen=(int)pos;
}
static void apply_csv_text_locked(const char *txt){
    pthread_mutex_lock(&g_csvLock);
    Item tmp[MAX_ITEMS]; int cnt=0;
    const char *p=txt;

    // Cabeçalho opcional de versão: #STAMP:<numero>
    long long remoteStamp = 0;
    for(;;){
        char firstLine[2048]={0};
        const char *nl=strchr(p,'\n');
        int n = nl ? (int)(nl-p) : (int)strlen(p);
        if(n<=0) break;
        if(n >= (int)sizeof(firstLine)) n = (int)sizeof(firstLine)-1;
        memcpy(firstLine,p,n); firstLine[n]=0;
        const char *next = nl ? (nl+1) : (p+n);

        if(strncmp(firstLine,"#STAMP:",7)==0){
            remoteStamp = atoll(firstLine+7);
            p = next;
            continue;
        }
        if(strncmp(firstLine,"sku,",4)==0){
            p = next;
            break;
        }
        // Sem cabeçalho conhecido: tenta interpretar desde o início.
        break;
    }

    // Se este pacote for mais velho que uma alteração local já feita, descarta.
    if(g_csvStamp > 0 && remoteStamp < g_csvStamp){
        pthread_mutex_unlock(&g_csvLock);
        return;
    }

    while(*p && cnt<MAX_ITEMS){
        const char *nl=strchr(p,'\n');
        int n2 = nl? (int)(nl-p) : (int)strlen(p);
        if(n2<=0) break;
        char line[2048];
        if(n2>(int)sizeof(line)-1) n2=(int)sizeof(line)-1;
        memcpy(line,p,n2); line[n2]=0;
        Item it;
        if(from_csv_line(line,&it) && cnt<MAX_ITEMS) tmp[cnt++]=it;
        p = nl? (nl+1) : (p+n2);
    }
    g_count = cnt;
    for(int i=0;i<cnt;i++) g_items[i]=tmp[i];
    if(remoteStamp > g_csvStamp) g_csvStamp = remoteStamp;

    // Salva o CSV recebido no disco SEM marcar g_dirty.
    // Antes, o Linux atualizava a tela, mas o estoque.csv local podia ficar antigo;
    // ao recarregar/sincronizar depois, itens apagados podiam voltar.
    FILE *f=fopen(CSV_FILE,"wb");
    if(f){
        fputs("sku,name,location,qty,price,cost_price\n",f);
        char line[1024];
        for(int i=0;i<g_count;i++){
            to_csv_line(line,sizeof(line),&g_items[i]);
            fputs(line,f);
        }
        fclose(f);
    }

    pthread_mutex_unlock(&g_csvLock);
    rebuild_filter(); // NÃO marca dirty aqui (foi recebido do servidor)
}


static void sales_file_to_text_locked(char **outBuf, int *outLen){
    pthread_mutex_lock(&g_csvLock);
    FILE *f = fopen(SALES_FILE, "rb");
    size_t cap = 4096, pos = 0;
    char *txt = (char*)malloc(cap);
    if(!txt){ pthread_mutex_unlock(&g_csvLock); if(outBuf) *outBuf=NULL; if(outLen) *outLen=0; return; }
    txt[0] = 0;
    if(f){
        char line[4096];
        while(fgets(line, sizeof(line), f)){
            size_t L = strlen(line);
            if(pos + L + 8 >= cap){ cap = (cap + L + 4096) * 2; txt = (char*)realloc(txt, cap); }
            memcpy(txt + pos, line, L); pos += L;
        }
        fclose(f);
    }
    if(pos == 0){
        const char *hdr = "datetime,seller,sku,name,qty,unit_cost,unit_price,line_cost,line_total,discount_pct,line_discount,line_net_total,line_profit\n";
        size_t L = strlen(hdr);
        if(pos + L + 1 >= cap){ cap = L + 32; txt = (char*)realloc(txt, cap); }
        memcpy(txt + pos, hdr, L); pos += L;
    }
    txt[pos] = 0;
    pthread_mutex_unlock(&g_csvLock);
    if(outBuf) *outBuf = txt;
    if(outLen) *outLen = (int)pos;
}
static int sales_line_exists(char **lines, int count, const char *line){
    for(int i=0;i<count;i++) if(strcmp(lines[i], line)==0) return 1;
    return 0;
}
static void merge_sales_text_locked(const char *txt){
    if(!txt || !*txt) return;
    pthread_mutex_lock(&g_csvLock);

    char **lines = NULL;
    int count = 0, cap = 0;
    char cur[4096];

    FILE *f = fopen(SALES_FILE, "rb");
    if(f){
        while(fgets(cur, sizeof(cur), f)){
            cur[strcspn(cur, "\r\n")] = 0;
            if(!cur[0]) continue;
            if(cap <= count){ cap = cap ? cap*2 : 256; lines = (char**)realloc(lines, sizeof(char*)*(size_t)cap); }
            lines[count++] = strdup(cur);
        }
        fclose(f);
    }

    char *copy = strdup(txt);
    if(copy){
        char *save = NULL;
        for(char *p = strtok_r(copy, "\n", &save); p; p = strtok_r(NULL, "\n", &save)){
            p[strcspn(p, "\r")] = 0;
            if(!p[0]) continue;
            if(starts_with_icase(p, "datetime,")){
                if(count == 0){
                    if(cap <= count){ cap = cap ? cap*2 : 256; lines = (char**)realloc(lines, sizeof(char*)*(size_t)cap); }
                    lines[count++] = strdup(p);
                }
                continue;
            }
            if(!sales_line_exists(lines, count, p)){
                if(cap <= count){ cap = cap ? cap*2 : 256; lines = (char**)realloc(lines, sizeof(char*)*(size_t)cap); }
                lines[count++] = strdup(p);
            }
        }
        free(copy);
    }

    f = fopen(SALES_FILE, "wb");
    if(f){
        if(count == 0 || !starts_with_icase(lines[0], "datetime,")){
            const char *hdr = "datetime,seller,sku,name,qty,unit_cost,unit_price,line_cost,line_total,discount_pct,line_discount,line_net_total,line_profit\n";
            fwrite(hdr,1,strlen(hdr),f);
        }
        for(int i=0;i<count;i++){
            fputs(lines[i], f);
            fputc('\n', f);
        }
        fclose(f);
    }
    for(int i=0;i<count;i++) free(lines[i]);
    free(lines);
    pthread_mutex_unlock(&g_csvLock);
}

// ---- Handler de frames inesperados (ex.: PUSH_CSV vindo do servidor) ----
static void handle_unsolicited_locked(const char *firstLine){
    if(strcmp(firstLine, CMD_PUSH_CSV)==0){
        // AJUSTE: se houver alterações locais pendentes (g_dirty!=0),
        // ler e DESCARTAR o payload para NÃO sobrescrever o que está em edição/remoção local.
        char *payload = (char*)malloc(1<<20);
        int r = recv_payload_until_dot_locked(payload, 1<<20);
        if(r>=0){
            if(g_dirty==0){
                apply_csv_text_locked(payload);
            }
            // else: ignorado (descartado de propósito)
        }
        free(payload);
    } else if(strcmp(firstLine, CMD_PUSH_SALES)==0){
        // Vendas são cumulativas, então pode mesclar mesmo se houver venda local pendente.
        char *payload = (char*)malloc(1<<20);
        int r = recv_payload_until_dot_locked(payload, 1<<20);
        if(r>=0) merge_sales_text_locked(payload);
        free(payload);
    } else {
        // consumir payload e descartar
        char dummy[1<<20];
        recv_payload_until_dot_locked(dummy, sizeof(dummy));
    }
}

// espera por uma resposta específica; enquanto isso, consome e processa PUSH_CSV
static int wait_for_cmd_locked(const char *expected){
    for(int i=0;i<12;i++){
        char line[NET_MAXLINE];
        int n = recv_line_locked(line,sizeof(line));
        if(n<=0) return -1;
        if(strcmp(line, expected)==0){
            return 0;
        }
        // qualquer outro comando inesperado: tratar (ex.: PUSH_CSV)
        handle_unsolicited_locked(line);
    }
    return -2;
}

// ---- Monta payload de impressão ----
// Formato simples, compatível com PrintPayloadReceiptDirect (lado Windows):
// SELLER:<nome>
// DISCOUNT:<pct>
// TYPE:SALE|QUOTE
// ITEMS:
// sku,name,qty,unit_price
static char* build_print_payload(int is_quote, float subtotal_hint, int *outLen){
    (void)subtotal_hint;
    size_t cap = 2048 + (size_t)((is_quote?g_quoteCount:g_cartCount) * 128);
    char *buf = (char*)malloc(cap);
    size_t pos = 0;
    const char *type = is_quote ? "QUOTE" : "SALE";
    int discount = is_quote ? g_quoteDiscPct : g_cartDiscPct;

    pos += snprintf(buf+pos, cap-pos,
                    "SELLER:%s\nDISCOUNT:%d\nTYPE:%s\nITEMS:\n",
                    g_seller[0]?g_seller:"-", discount, type);
    pos += snprintf(buf+pos, cap-pos, "sku,name,qty,unit_price\n");

    if(!is_quote){
        for(int i=0;i<g_cartCount;i++){
            Item *it=&g_items[g_cart[i].itemIndex];
            pos += snprintf(buf+pos, cap-pos,
                            "%s,%s,%d,%.2f\n",
                            it->sku, it->name, g_cart[i].qty, it->price);
        }
    }else{
        for(int i=0;i<g_quoteCount;i++){
            Item *it=&g_items[g_quote[i].itemIndex];
            pos += snprintf(buf+pos, cap-pos,
                            "%s,%s,%d,%.2f\n",
                            it->sku, it->name, g_quote[i].qty, it->price);
        }
    }

    if(outLen) *outLen = (int)pos;
    return buf;
}

// ---- Operações de rede de alto nível ----
static int net_pull_csv_locked(void){
    if(send_cmd_payload_locked(CMD_PULL_CSV, NULL)<0) return -1;
    if(wait_for_cmd_locked(CMD_ACK)!=0) return -2;
    char *payload = (char*)malloc(1<<20);
    int r = recv_payload_until_dot_locked(payload, 1<<20);
    if(r>=0){
        // Só aplica se NÃO há alteração local pendente.
        if(g_dirty==0) apply_csv_text_locked(payload);
        free(payload);
        return 0;
    }
    free(payload);
    return -3;
}
static int net_push_csv_locked(void){
    char *csv=NULL; int len=0; csv_to_text_locked(&csv,&len);
    int rc = send_cmd_payload_locked(CMD_PUSH_CSV, csv);
    free(csv);
    if(rc<0) return -1;
    if(wait_for_cmd_locked(CMD_ACK)!=0) return -2;
    char payload[NET_MAXLINE];
    recv_payload_until_dot_locked(payload,sizeof(payload)); // "SYNCED\n" / ignora
    return 0;
}

static int net_pull_sales_locked(void){
    if(send_cmd_payload_locked(CMD_PULL_SALES, NULL)<0) return -1;
    if(wait_for_cmd_locked(CMD_ACK)!=0) return -2;
    char *payload = (char*)malloc(1<<20);
    int r = recv_payload_until_dot_locked(payload, 1<<20);
    if(r>=0){
        merge_sales_text_locked(payload);
        free(payload);
        return 0;
    }
    free(payload);
    return -3;
}
static int net_push_sales_locked(void){
    char *sales=NULL; int len=0;
    sales_file_to_text_locked(&sales,&len);
    int rc = send_cmd_payload_locked(CMD_PUSH_SALES, sales?sales:"");
    free(sales);
    if(rc<0) return -1;
    if(wait_for_cmd_locked(CMD_ACK)!=0) return -2;
    char payload[NET_MAXLINE];
    recv_payload_until_dot_locked(payload,sizeof(payload));
    return 0;
}
static int net_print_with_payload_locked(int is_quote){
    int dummyLen=0;
    char *payload = build_print_payload(is_quote, 0.0f, &dummyLen);
    const char *cmd = is_quote ? CMD_PRINT_QUOTE : CMD_PRINT_SALE;
    int rc = send_cmd_payload_locked(cmd, payload);
    free(payload);
    if(rc<0) return -1;
    int w = wait_for_cmd_locked(CMD_ACK);
    if(w!=0) return -2;
    char msg[NET_MAXLINE];
    recv_payload_until_dot_locked(msg,sizeof(msg)); // "PRINTED\n" ou similar
    return 0;
}

static void* net_thread(void *arg){
    (void)arg;
    double lastPing=0.0, lastPull=0.0, lastSalesPull=0.0;
    for(;;){
        double now = GetTime();
        // (1) Conecta se necessário
        if(!g_connected){
            SOCKET s = tcp_connect_blocking(IP_WINDOWS, NET_TCP_PORT);
            pthread_mutex_lock(&g_sockLock);
            if(s>=0){ g_sock=s; g_connected=1; }
            pthread_mutex_unlock(&g_sockLock);
            msleep(1500);
            continue;
        }

        // (2) Se ficou "dirty", tenta empurrar já
        if(g_dirty){
            pthread_mutex_lock(&g_sockLock);
            if(g_connected){
                if(net_push_csv_locked()==0){
                    g_dirty = 0; // limpo após sucesso
                    // Evita fazer PULL imediatamente depois do PUSH local, o que podia
                    // reaplicar uma resposta antiga em redes lentas.
                    lastPull = GetTime();
                } else {
                    // erro => fecha e reconecta
                    net_close_locked();
                }
            }
            pthread_mutex_unlock(&g_sockLock);
        }

        // (2b) Se há vendas locais novas, sincroniza o vendas.csv também
        if(g_salesDirty){
            pthread_mutex_lock(&g_sockLock);
            if(g_connected){
                if(net_push_sales_locked()==0){
                    g_salesDirty = 0;
                    lastSalesPull = GetTime();
                } else {
                    net_close_locked();
                }
            }
            pthread_mutex_unlock(&g_sockLock);
        }

        // (3) Ping a cada ~2s para detectar queda rápida
        if(now - lastPing > 2.0){
            pthread_mutex_lock(&g_sockLock);
            if(g_connected){
                if(send_cmd_payload_locked(CMD_PING,"OK\n")<0){
                    net_close_locked();
                } else {
                    if(wait_for_cmd_locked(CMD_PONG)!=0){
                        net_close_locked();
                    } else {
                        char dummy[NET_MAXLINE]; recv_payload_until_dot_locked(dummy,sizeof(dummy));
                    }
                }
            }
            pthread_mutex_unlock(&g_sockLock);
            lastPing = now;
        }

        // (4) Pull a cada ~5s — mas SOMENTE se NÃO houver alterações locais pendentes
        if((now - lastPull > 5.0) && (g_dirty==0)){
            pthread_mutex_lock(&g_sockLock);
            if(g_connected){
                if(net_pull_csv_locked()!=0){ net_close_locked(); }
            }
            pthread_mutex_unlock(&g_sockLock);
            lastPull = now;
        }

        // (5) Sincroniza vendas periodicamente. Como vendas são cumulativas, usa merge.
        if(now - lastSalesPull > 5.0){
            pthread_mutex_lock(&g_sockLock);
            if(g_connected){
                if(net_pull_sales_locked()!=0){ net_close_locked(); }
            }
            pthread_mutex_unlock(&g_sockLock);
            lastSalesPull = now;
        }

        msleep(50);
    }
    return NULL;
}
#endif // !_WIN32

// -------------------- Atualiza status "impressora" (proxy) --------------------
static void UpdatePrinterStatus(void){
    double now = GetTime();
    if(now - g_prnLastCheck < g_prnInterval) return;
    g_prnLastCheck = now;
#ifndef _WIN32
    g_prnConnected = (g_connected != 0);
#else
    g_prnConnected = false;
#endif
}

// ===================================================
// ===================== MAIN ========================
// ===================================================
int main(void){
#ifndef _WIN32
    // Thread de rede (Linux cliente)
    pthread_t th; pthread_create(&th,NULL,net_thread,NULL);
#endif

    SetConfigFlags(FLAG_WINDOW_RESIZABLE);
    InitWindow(1200,700,"Estoque de Componentes (Linux)");
    SetTargetFPS(60);
    ApplyTheme();

    load_csv(CSV_FILE);
    rebuild_filter();

    const int rowHeight=34, headerHeight=36;

    while(!WindowShouldClose()){
        // Atualiza status de "impressora" (proxy Windows)
        UpdatePrinterStatus();

        // atalhos
        if(IsKeyPressed(KEY_F5) && g_modal==MODAL_NONE){
            load_csv(CSV_FILE); rebuild_filter();
        }
        if(IsKeyPressed(KEY_F6) && g_modal==MODAL_NONE){
            save_csv(CSV_FILE);
        }

        BeginDrawing();
        ClearBackground(COL_BG);
        // --- CORREÇÃO: RESET DO SCROLL AO PESQUISAR ---
        // Isso impede que o scroll fique "preso" em um índice inválido 
        // quando o filtro de busca reduz a quantidade de itens.
        static char g_lastSearch[128] = "\0"; 
        if (strcmp(g_search, g_lastSearch) != 0) {
            strcpy(g_lastSearch, g_search);
            g_scrollIndex = 0; 
        }
        // ----------------------------------------------

        // Barra superior
        Rectangle top = (Rectangle){0,0,(float)GetScreenWidth(),60};
        DrawRectangleRec(top, (Color){0,0,0,32});
        const char *title="Estoque de Componentes";
        int titleFont=24, titleW=MeasureText(title,titleFont);
        int xStart=16, yTop=12;
        DrawText(title,xStart,yTop+3,titleFont,COL_TEXT);
        int xButtons=xStart+titleW+24;
        Rectangle rAdd  ={(float)xButtons, (float)yTop,110,36};
        Rectangle rEdit ={(float)(xButtons+118), (float)yTop,110,36};
        Rectangle rDel  ={(float)(xButtons+236), (float)yTop,110,36};
        Rectangle rCart ={(float)(xButtons+354), (float)yTop,120,36};
        Rectangle rQuote={(float)(xButtons+354+128),(float)yTop,120,36};
        Rectangle rSales={(float)(xButtons+354+128+128),(float)yTop,120,36};
        float searchX=rSales.x+rSales.width+24;
        Rectangle rSearch={searchX,(float)yTop,(float)(GetScreenWidth()-searchX-16),36};

        // Busca
        if(g_modal==MODAL_NONE){
            bool toggled = DrawTextInputBoxAdv(rSearch, g_search, sizeof(g_search), &g_searchEdit);
            (void)toggled;
            if(IsKeyPressed(KEY_ENTER)||IsKeyPressed(KEY_KP_ENTER)) rebuild_filter();
        } else {
            DrawTextInputBoxAdv(rSearch, g_search, sizeof(g_search), &(bool){false});
        }

        // Botões topo
        if(g_modal==MODAL_NONE){
            if(GuiButton(rAdd,"Adicionar")){
                memset(&g_form,0,sizeof(g_form)); g_form.qty=0; g_form.price=0.0f; g_form.cost_price=0.0f;
                g_modal=MODAL_ADD; g_searchEdit=false;
                g_editSku=true; g_editName=false; g_editLoc=false; g_editPrice=false; g_editCost=false; g_qtyEdit=false;
            }
            if(GuiButton(rEdit,"Editar")){
                if(g_selected>=0 && g_selected<filtered_count){
                    g_form=g_items[ filtered_indices[g_selected] ];
                    g_modal=MODAL_EDIT; g_searchEdit=false;
                    g_editSku=true; g_editName=false; g_editLoc=false; g_editPrice=false; g_editCost=false; g_qtyEdit=false;
                }
            }
            if(GuiButton(rDel,"Apagar")){
                if(g_selected>=0 && g_selected<filtered_count){
                    g_modal=MODAL_CONFIRM_DELETE; g_searchEdit=false;
                }
            }
            if(GuiButton(rCart,"Carrinho")){
                g_modal=MODAL_CART; g_searchEdit=false; cartSel=-1; cartScroll=0; cartStatus[0]=0;
            }
            if(GuiButton(rQuote,"Orçamento")){
                g_modal=MODAL_QUOTE; g_searchEdit=false; quoteSel=-1; quoteScroll=0; quoteStatus[0]=0;
            }
            if(GuiButton(rSales,"Vendas mês")){
                g_modal=MODAL_MONTH_SALES; g_searchEdit=false; g_salesScroll=0;
            }
        } else {
            GuiButton(rAdd,"Adicionar"); GuiButton(rEdit,"Editar");
            GuiButton(rDel,"Apagar");   GuiButton(rCart,"Carrinho"); GuiButton(rQuote,"Orçamento"); GuiButton(rSales,"Vendas mês");
        }

        // Tabela
        int left=16, right=GetScreenWidth()-16, topY=76, bottom=GetScreenHeight()-16;
        Rectangle rHeader={left,(float)topY,(float)(right-left),(float)headerHeight};
        DrawRectangleRec(rHeader, COL_HEADER);
        int c1=180,c2=610,c3=760,c4=840,c5=960;
        DrawText("SKU",   left+8, topY+8,18,COL_TEXT);
        DrawText("Nome",  c1+8,   topY+8,18,COL_TEXT);
        DrawText("Local", c2+8,   topY+8,18,COL_TEXT);
        DrawText("Qtd",   c3+8,   topY+8,18,COL_TEXT);
        DrawText("Preço", c4+8,   topY+8,18,COL_TEXT);
        DrawText("Custo", c5+8,   topY+8,18,COL_TEXT);

        Rectangle rList={left,topY+headerHeight,(float)(right-left),(float)(bottom-(topY+headerHeight))};
        DrawRectangleRec(rList, COL_PANEL);
        int visibleRows=(int)(rList.height/rowHeight); if(visibleRows<0) visibleRows=0;

        if(g_modal==MODAL_NONE && CheckCollisionPointRec(GetMousePosition(), rList)){
            int wheel=GetMouseWheelMove();
            if(wheel){
                g_scrollIndex-=wheel; if(g_scrollIndex<0) g_scrollIndex=0;
                int maxScroll=(filtered_count>visibleRows)?(filtered_count-visibleRows):0;
                if(g_scrollIndex>maxScroll) g_scrollIndex=maxScroll;
            }
        }

        for(int r=0;r<visibleRows;r++){
            int idx=g_scrollIndex+r; if(idx>=filtered_count) break;
            int real=filtered_indices[idx];
            int y=(int)(rList.y+r*rowHeight);
            Rectangle rowRect={rList.x,(float)y,rList.width,(float)rowHeight};
            bool selected=(g_selected==idx);
            DrawRectangleRec(rowRect, (r%2==0)?COL_ROW_A:COL_ROW_B);
            if(selected) DrawRectangleRec(rowRect, COL_HILIGHT);
            if(g_modal==MODAL_NONE && CheckCollisionPointRec(GetMousePosition(),rowRect) &&
               IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) g_selected=idx;

            DrawText(g_items[real].sku, left+8, y+8,18,COL_TEXT);
            DrawTextClippedCell(g_items[real].name, (Rectangle){(float)c1+8, (float)y+8, (float)(c2-c1-16), (float)(rowHeight-10)}, 18, COL_TEXT);
            DrawTextClippedCell(g_items[real].location, (Rectangle){(float)c2+8, (float)y+8, (float)(c3-c2-16), (float)(rowHeight-10)}, 18, COL_TEXT_DIM);
            DrawText(TextFormat("%d",g_items[real].qty), c3+8, y+8,18,COL_TEXT);
            DrawText(TextFormat("R$ %.2f", g_items[real].price), c4+8, y+8,18,COL_TEXT);
            DrawText(TextFormat("R$ %.2f", g_items[real].cost_price), c5+8, y+8,18,COL_TEXT_DIM);

            // Botão [+] à direita -> abrir modal (escolher carrinho/orçamento)
            Rectangle rPlus = { rList.x + rList.width - 44, (float)y+4, 36, rowHeight-8 };
            if(g_modal==MODAL_NONE){
                if(GuiButton(rPlus, "+")){
                    addCart_itemIndex = real; addCart_qty = 1; addCart_qtyEdit = false;
                    g_modal = MODAL_ADD_CHOOSE; g_searchEdit = false;
                }
            } else { GuiButton(rPlus, "+"); }
        }

        // Rodapé
        Rectangle rFooter={16,(float)(GetScreenHeight()-48),(float)(GetScreenWidth()-32),32};
        if(g_modal==MODAL_NONE){
            if(GuiButton((Rectangle){rFooter.x,rFooter.y,120,rFooter.height},"Salvar CSV (F6)")) save_csv(CSV_FILE);
            if(GuiButton((Rectangle){rFooter.x+128,rFooter.y,140,rFooter.height},"Carregar CSV (F5)")){
                load_csv(CSV_FILE); rebuild_filter();
            }
        } else {
            GuiButton((Rectangle){rFooter.x,rFooter.y,120,rFooter.height},"Salvar CSV (F6)");
            GuiButton((Rectangle){rFooter.x+128,rFooter.y,140,rFooter.height},"Carregar CSV (F5)");
        }

        DrawText(TextFormat("Itens: %d | Filtrados: %d | Carrinho: %d | Orçamento: %d | Peer: %s (%s)",
                 g_count, filtered_count, g_cartCount, g_quoteCount,
#ifndef _WIN32
                 g_connected ? "Conectado" : "Desconectado",
#else
                 "N/A",
#endif
                 IP_WINDOWS ), rFooter.x+280, rFooter.y+6,20,COL_TEXT_DIM);

        // Status "Impressora"
        const char *pstatus = g_prnConnected ? "Impressora: via Windows" : "Impressora: (sem Windows)";
        int sw = MeasureText(pstatus, 18);
        int sx = (int)(rFooter.x + rFooter.width - sw);
        DrawText(pstatus, sx, (int)rFooter.y+6, 18,
                 g_prnConnected ? (Color){120,200,120,255} : (Color){220,120,120,255});

        // ---------- Overlays/Modais ----------
        if(g_modal != MODAL_NONE){
            DrawRectangle(0,0,GetScreenWidth(),GetScreenHeight(), (Color){0,0,0,48});
        }

        if(g_modal==MODAL_ADD || g_modal==MODAL_EDIT){
            Rectangle panel={GetScreenWidth()/2.0f-380, GetScreenHeight()/2.0f-220, 760, 400};
            DrawRectangleRec(panel, COL_PANEL);
            DrawRectangleLinesEx(panel,2,(Color){122,61,245,255});
            DrawText((g_modal==MODAL_ADD? "Adicionar Item":"Editar Item"), panel.x+16, panel.y+12,24,COL_TEXT);

            int y=(int)panel.y+60; int x1=(int)panel.x+16; int w1=320, h=36;

            DrawText("SKU", x1, y-22,18,COL_TEXT_DIM);
            if(DrawTextInputBoxAdv((Rectangle){x1,y,w1,h}, g_form.sku, sizeof(g_form.sku), &g_editSku)) {
                g_editName=false; g_editLoc=false; g_editPrice=false; g_editCost=false; g_qtyEdit=false;
            }
            DrawText("Nome", x1+360, y-22,18,COL_TEXT_DIM);
            if(DrawTextInputBoxAdv((Rectangle){x1+360,y,w1,h}, g_form.name, sizeof(g_form.name), &g_editName)) {
                g_editSku=false; g_editLoc=false; g_editPrice=false; g_editCost=false; g_qtyEdit=false;
            }
            y+=64;
            DrawText("Local", x1, y-22,18,COL_TEXT_DIM);
            if(DrawTextInputBoxAdv((Rectangle){x1,y,w1,h}, g_form.location, sizeof(g_form.location), &g_editLoc)) {
                g_editSku=false; g_editName=false; g_editPrice=false; g_editCost=false; g_qtyEdit=false;
            }
            DrawText("Quantidade", x1+360, y-22,18,COL_TEXT_DIM);
            if (GuiSpinner((Rectangle){x1+360,y,140,h}, NULL, &g_form.qty, 0, 100000, g_qtyEdit)) {
                g_qtyEdit = !g_qtyEdit;
            }
            y+=64;
            DrawText("Preço venda (R$)", x1, y-22,18,COL_TEXT_DIM);
            static int priceCents=0;
            static bool priceBufInit=false;
            if(!priceBufInit){ priceCents = money_to_cents(g_form.price); priceBufInit=true; }
            Rectangle priceRect = (Rectangle){x1,y,160,h};
            if(IsMouseButtonPressed(MOUSE_LEFT_BUTTON) && CheckCollisionPointRec(GetMousePosition(), priceRect)){
                g_editPrice=true; g_editSku=false; g_editName=false; g_editLoc=false; g_editCost=false; g_qtyEdit=false;
            }
            DrawMoneyInputBox(priceRect, &priceCents, g_editPrice);

            DrawText("Preço custo (R$)", x1+200, y-22,18,COL_TEXT_DIM);
            static int costCents=0;
            static bool costBufInit=false;
            if(!costBufInit){ costCents = money_to_cents(g_form.cost_price); costBufInit=true; }
            Rectangle costRect = (Rectangle){x1+200,y,160,h};
            if(IsMouseButtonPressed(MOUSE_LEFT_BUTTON) && CheckCollisionPointRec(GetMousePosition(), costRect)){
                g_editCost=true; g_editSku=false; g_editName=false; g_editLoc=false; g_editPrice=false; g_qtyEdit=false;
            }
            DrawMoneyInputBox(costRect, &costCents, g_editCost);

            if(IsKeyPressed(KEY_TAB)){
                if(g_editSku){ g_editSku=false; g_editName=true; }
                else if(g_editName){ g_editName=false; g_editLoc=true; }
                else if(g_editLoc){ g_editLoc=false; g_editPrice=true; }
                else if(g_editPrice){ g_editPrice=false; g_editCost=true; }
                else if(g_editCost){ g_editCost=false; g_editSku=true; }
                else { g_editSku=true; } g_qtyEdit=false;
            }

            y+=80;
            bool ok = GuiButton((Rectangle){x1, y,140,40},"OK");
            bool cancel = GuiButton((Rectangle){x1+160, y,140,40},"Cancelar");
            if(ok){
                trim(g_form.sku); trim(g_form.name); trim(g_form.location);
                g_form.price = (float)priceCents / 100.0f;
                g_form.cost_price = (float)costCents / 100.0f;
                if(g_form.sku[0] && g_form.name[0]){
                    if(g_modal==MODAL_ADD) add_item(&g_form);
                    else update_item(filtered_indices[g_selected], &g_form);
                    rebuild_filter();
                    g_modal=MODAL_NONE; g_searchEdit=false; priceBufInit=false; costBufInit=false;
                    save_csv(CSV_FILE); // já marca g_dirty=1
                }
            }
            if(cancel){ g_modal=MODAL_NONE; g_searchEdit=false; priceBufInit=false; costBufInit=false; }

        } else if(g_modal==MODAL_CONFIRM_DELETE){
            Rectangle panel={GetScreenWidth()/2.0f-250, GetScreenHeight()/2.0f-100, 500, 180};
            DrawRectangleRec(panel, COL_PANEL);
            DrawRectangleLinesEx(panel,2,COL_WARN);
            DrawText("Apagar item selecionado?", panel.x+16, panel.y+20,24,COL_WARN);
            bool yes=GuiButton((Rectangle){panel.x+60, panel.y+100,140,40},"Sim");
            bool no =GuiButton((Rectangle){panel.x+300,panel.y+100,140,40},"Não");
            if(yes){
                if(g_selected>=0 && g_selected<filtered_count){
                    delete_item(filtered_indices[g_selected]);
                    rebuild_filter();
                    save_csv(CSV_FILE); // marca g_dirty=1 e empurra; PUSH do servidor será ignorado se dirty!=0
                }
                g_modal=MODAL_NONE; g_searchEdit=false;
            }
            if(no){ g_modal=MODAL_NONE; g_searchEdit=false; }

        } else if(g_modal==MODAL_ADD_CHOOSE){
            Rectangle panel={GetScreenWidth()/2.0f-260, GetScreenHeight()/2.0f-150, 520, 300};
            DrawRectangleRec(panel, COL_PANEL);
            DrawRectangleLinesEx(panel,2,(Color){122,61,245,255});
            DrawText("Adicionar item", panel.x+16, panel.y+12,24,COL_TEXT);

            if(addCart_itemIndex>=0 && addCart_itemIndex<g_count){
                Item *it = &g_items[addCart_itemIndex];
                bool haveStock = (it->qty > 0);
                int maxForSpinner = haveStock ? it->qty : 9999;
                if(addCart_qty > maxForSpinner) addCart_qty = maxForSpinner;
                if(addCart_qty < 1) addCart_qty = 1;

                DrawText(TextFormat("SKU: %s", it->sku), panel.x+16, panel.y+52, 18, COL_TEXT);
                DrawText(TextFormat("%.48s", it->name), panel.x+16, panel.y+74, 18, COL_TEXT_DIM);
                DrawText(TextFormat("Disponível: %d Preço: R$ %.2f", it->qty, it->price), panel.x+16, panel.y+96, 18, COL_TEXT_DIM);
                DrawText("Qtd:", panel.x+16, panel.y+136, 18, COL_TEXT_DIM);
                if(GuiSpinner((Rectangle){panel.x+60, panel.y+132, 110, 30}, NULL, &addCart_qty, 1, maxForSpinner, addCart_qtyEdit)){
                    addCart_qtyEdit = !addCart_qtyEdit;
                }

                bool addToCart  = GuiButton((Rectangle){panel.x+190, panel.y+128, 150, 36}, haveStock? "Ao Carrinho":"Sem estoque");
                bool addToQuote = GuiButton((Rectangle){panel.x+350, panel.y+128, 150, 36}, "Ao Orçamento");
                bool close      = GuiButton((Rectangle){panel.x+panel.width-100, panel.y+panel.height-48, 84, 30}, "Fechar");

                if(addToCart && haveStock){
                    int q = addCart_qty; if(q < 1) q = 1; if(q > it->qty) q = it->qty;
                    if(q > 0) cartAdd(addCart_itemIndex, q);
                    g_modal = MODAL_NONE; // fica na lista; usuário pode abrir carrinho
                }
                if(addToQuote){
                    int q = addCart_qty; if(q < 1) q = 1; if(q > 9999) q = 9999;
                    quoteAdd(addCart_itemIndex, q);
                    g_modal = MODAL_NONE;
                }
                if(close){ g_modal = MODAL_NONE; }
            } else {
                DrawText("Item inválido.", panel.x+16, panel.y+72, 20, COL_WARN);
                if(GuiButton((Rectangle){panel.x+panel.width-96, panel.y+panel.height-46, 80, 30}, "Fechar")){
                    g_modal = MODAL_NONE;
                }
            }

        } else if(g_modal==MODAL_CART){
            Rectangle panel={GetScreenWidth()/2.0f-520, GetScreenHeight()/2.0f-270, 1040, 540};
            DrawRectangleRec(panel, COL_PANEL);
            DrawRectangleLinesEx(panel,2,(Color){122,61,245,255});
            DrawText("Carrinho de Vendas", panel.x+16, panel.y+12,24,COL_TEXT);
            DrawText("Vendedor", panel.x+16, panel.y+52,18,COL_TEXT_DIM);
            static bool sellerEdit=true;
            if(DrawTextInputBoxAdv((Rectangle){panel.x+16, panel.y+72, 280, 36}, g_seller, sizeof(g_seller), &sellerEdit)) { }

            // Desconto %
            DrawText("Desc %", panel.x+320, panel.y+52,18,COL_TEXT_DIM);
            if(GuiSpinner((Rectangle){panel.x+320, panel.y+72, 120, 36}, NULL, &g_cartDiscPct, 0, 100, g_cartDiscEdit)) g_cartDiscEdit = !g_cartDiscEdit;

            Rectangle cartHeader={panel.x+16, panel.y+120, panel.width-32, 30};
            DrawRectangleRec(cartHeader, COL_HEADER);
            int xSku= (int)cartHeader.x+8;
            int xName=(int)cartHeader.x+160;
            int xQty =(int)cartHeader.x+520;
            int xPrice=(int)cartHeader.x+600;
            int xTotal=(int)cartHeader.x+740;
            DrawText("SKU", xSku, cartHeader.y+6,18,COL_TEXT);
            DrawText("Nome", xName, cartHeader.y+6,18,COL_TEXT);
            DrawText("Qtd",  xQty,  cartHeader.y+6,18,COL_TEXT);
            DrawText("Preço",xPrice,cartHeader.y+6,18,COL_TEXT);
            DrawText("Total",xTotal,cartHeader.y+6,18,COL_TEXT);

            Rectangle cartList={panel.x+16, panel.y+150, panel.width-32, 260};
            DrawRectangleRec(cartList, COL_PANEL);
            int rows=(int)(cartList.height/30); if(rows<0) rows=0;
            if(CheckCollisionPointRec(GetMousePosition(), cartList)){
                int wheel=GetMouseWheelMove();
                if(wheel){
                    cartScroll-=wheel; if(cartScroll<0) cartScroll=0;
                    int maxS=(g_cartCount>rows)?(g_cartCount-rows):0;
                    if(cartScroll>maxS) cartScroll=maxS;
                }
            }

            float subtotalAll = 0.0f;
            for (int i = 0; i < g_cartCount; i++) {
                Item *itAll = &g_items[g_cart[i].itemIndex];
                subtotalAll += itAll->price * (float)g_cart[i].qty;
            }

            for(int r=0;r<rows;r++){
                int idx=cartScroll+r; if(idx>=g_cartCount) break;
                int y=(int)(cartList.y+r*30);
                Rectangle row={cartList.x,(float)y,cartList.width,30};
                DrawRectangleRec(row, (r%2==0)?COL_ROW_A:COL_ROW_B);
                if(cartSel==idx) DrawRectangleRec(row, COL_HILIGHT);
                if(CheckCollisionPointRec(GetMousePosition(),row) && IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) cartSel=idx;

                Item *it=&g_items[g_cart[idx].itemIndex];
                float lt = it->price * (float)g_cart[idx].qty;
                DrawText(TextFormat("%-12s", it->sku), xSku,   y+7,18,COL_TEXT);
                DrawText(TextFormat("%-48.48s", it->name), xName,  y+7,18,COL_TEXT);
                DrawText(TextFormat("%d", g_cart[idx].qty), xQty,  y+7,18,COL_TEXT);
                DrawText(TextFormat("R$ %.2f", it->price), xPrice,y+7,18,COL_TEXT);
                DrawText(TextFormat("R$ %.2f", lt), xTotal, y+7,18,COL_TEXT);
            }

            float disc = subtotalAll * (g_cartDiscPct/100.0f);
            float totalGeral = subtotalAll - disc; if(totalGeral<0) totalGeral=0;
            DrawText(TextFormat("Subtotal: R$ %.2f Desconto: %d%% (-R$ %.2f) Total: R$ %.2f",
                                subtotalAll, g_cartDiscPct, disc, totalGeral),
                     panel.x+16, panel.y+420, 24, COL_TEXT);

            if(GuiButton((Rectangle){panel.x+16, panel.y+456, 130, 36}, "Remover")){
                if(cartSel>=0) cartRemoveAt(cartSel);
            }
            if(GuiButton((Rectangle){panel.x+156, panel.y+456, 160, 36}, "Limpar carrinho")){
                cartClear();
            }

            bool ok = GuiButton((Rectangle){panel.x+panel.width-340, panel.y+456, 160, 36}, "Finalizar venda");
            bool cancel = GuiButton((Rectangle){panel.x+panel.width-170, panel.y+456, 140, 36}, "Cancelar");

            if(ok){
                trim(g_seller);
                if(!g_seller[0]) snprintf(cartStatus,sizeof(cartStatus),"Informe o nome do vendedor.");
                else if(g_cartCount==0) snprintf(cartStatus,sizeof(cartStatus),"Carrinho vazio.");
                else {
                    int invalid=-1;
                    for(int i=0;i<g_cartCount;i++){
                        int have=g_items[g_cart[i].itemIndex].qty;
                        if(g_cart[i].qty<=0 || g_cart[i].qty>have){ invalid=i; break; }
                    }
                    if(invalid>=0){
                        Item *it=&g_items[g_cart[invalid].itemIndex];
                        snprintf(cartStatus,sizeof(cartStatus),"Estoque insuficiente para %s (disp. %d)", it->sku, it->qty);
                    } else {
                        // Baixa local com trava, para a thread de rede não ler g_items no meio da alteração.
                        pthread_mutex_lock(&g_csvLock);
                        for(int i=0;i<g_cartCount;i++) g_items[g_cart[i].itemIndex].qty -= g_cart[i].qty;
                        pthread_mutex_unlock(&g_csvLock);
                        append_sales_csv(g_seller, g_cart, g_cartCount, g_cartDiscPct);
                        save_csv(CSV_FILE); // marca dirty
                        rebuild_filter();
#ifndef _WIN32
                        // Impressão via Windows com PAYLOAD
                        pthread_mutex_lock(&g_sockLock);
                        if(g_connected){
                            int pr = net_print_with_payload_locked(0);
                            if(pr!=0) snprintf(cartStatus,sizeof(cartStatus),"Sem impressão (Windows indisponível).");
                            else cartStatus[0]=0;
                        } else {
                            snprintf(cartStatus,sizeof(cartStatus),"Sem impressão (desconectado).");
                        }
                        pthread_mutex_unlock(&g_sockLock);
#endif
                        cartClear();
                        g_modal=MODAL_NONE; g_searchEdit=false;
                    }
                }
            }
            if(cancel){ g_modal=MODAL_NONE; g_searchEdit=false; cartStatus[0]=0; }
            if(cartStatus[0]) DrawText(cartStatus, panel.x+340, panel.y+462, 20, COL_WARN);

        } else if(g_modal==MODAL_MONTH_SALES){
            DrawMonthlySalesModal();

        } else if(g_modal==MODAL_QUOTE){
            Rectangle panel={GetScreenWidth()/2.0f-520, GetScreenHeight()/2.0f-270, 1040, 540};
            DrawRectangleRec(panel, COL_PANEL);
            DrawRectangleLinesEx(panel,2,(Color){122,61,245,255});
            DrawText("Orçamento", panel.x+16, panel.y+12,24,COL_TEXT);
            DrawText("Vendedor", panel.x+16, panel.y+52,18,COL_TEXT_DIM);
            static bool sellerEdit2=false;
            if(DrawTextInputBoxAdv((Rectangle){panel.x+16, panel.y+72, 280, 36}, g_seller, sizeof(g_seller), &sellerEdit2)) { }

            DrawText("Desc %", panel.x+320, panel.y+52,18,COL_TEXT_DIM);
            if(GuiSpinner((Rectangle){panel.x+320, panel.y+72, 120, 36}, NULL, &g_quoteDiscPct, 0, 100, g_quoteDiscEdit)) g_quoteDiscEdit = !g_quoteDiscEdit;

            Rectangle cartHeader={panel.x+16, panel.y+120, panel.width-32, 30};
            DrawRectangleRec(cartHeader, COL_HEADER);
            int xSku= (int)cartHeader.x+8;
            int xName=(int)cartHeader.x+160;
            int xQty =(int)cartHeader.x+520;
            int xPrice=(int)cartHeader.x+600;
            int xTotal=(int)cartHeader.x+740;
            DrawText("SKU",   xSku,   cartHeader.y+6,18,COL_TEXT);
            DrawText("Nome",  xName,  cartHeader.y+6,18,COL_TEXT);
            DrawText("Qtd",   xQty,   cartHeader.y+6,18,COL_TEXT);
            DrawText("Preço", xPrice, cartHeader.y+6,18,COL_TEXT);
            DrawText("Total", xTotal, cartHeader.y+6,18,COL_TEXT);

            Rectangle cartList={panel.x+16, panel.y+150, panel.width-32, 260};
            DrawRectangleRec(cartList, COL_PANEL);
            int rows=(int)(cartList.height/30); if(rows<0) rows=0;
            if(CheckCollisionPointRec(GetMousePosition(), cartList)){
                int wheel=GetMouseWheelMove();
                if(wheel){
                    quoteScroll-=wheel; if(quoteScroll<0) quoteScroll=0;
                    int maxS=(g_quoteCount>rows)?(g_quoteCount-rows):0;
                    if(quoteScroll>maxS) quoteScroll=maxS;
                }
            }

            float subtotalAll = 0.0f;
            for (int i = 0; i < g_quoteCount; i++) {
                Item *itAll = &g_items[g_quote[i].itemIndex];
                subtotalAll += itAll->price * (float)g_quote[i].qty;
            }

            for(int r=0;r<rows;r++){
                int idx=quoteScroll+r; if(idx>=g_quoteCount) break;
                int y=(int)(cartList.y+r*30);
                Rectangle row={cartList.x,(float)y,cartList.width,30};
                DrawRectangleRec(row, (r%2==0)?COL_ROW_A:COL_ROW_B);
                if(quoteSel==idx) DrawRectangleRec(row, COL_HILIGHT);
                if(CheckCollisionPointRec(GetMousePosition(),row) && IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) quoteSel=idx;

                Item *it=&g_items[g_quote[idx].itemIndex];
                float lt = it->price * (float)g_quote[idx].qty;
                DrawText(TextFormat("%-12s", it->sku), xSku,   y+7,18,COL_TEXT);
                DrawText(TextFormat("%-48.48s", it->name), xName,  y+7,18,COL_TEXT);
                DrawText(TextFormat("%d", g_quote[idx].qty), xQty,  y+7,18,COL_TEXT);
                DrawText(TextFormat("R$ %.2f", it->price), xPrice,y+7,18,COL_TEXT);
                DrawText(TextFormat("R$ %.2f", lt), xTotal, y+7,18,COL_TEXT);
            }

            float disc = subtotalAll * (g_quoteDiscPct/100.0f);
            float totalGeral = subtotalAll - disc; if(totalGeral<0) totalGeral=0;
            DrawText(TextFormat("Subtotal: R$ %.2f Desconto: %d%% (-R$ %.2f) Total Orç.: R$ %.2f",
                                subtotalAll, g_quoteDiscPct, disc, totalGeral),
                     panel.x+16, panel.y+420, 24, COL_TEXT);

            if(GuiButton((Rectangle){panel.x+16, panel.y+456, 130, 36}, "Remover")){
                if(quoteSel>=0) quoteRemoveAt(quoteSel);
            }
            if(GuiButton((Rectangle){panel.x+156, panel.y+456, 160, 36}, "Limpar orçamento")){
                quoteClear();
            }

            bool ok = GuiButton((Rectangle){panel.x+panel.width-360, panel.y+456, 180, 36}, "Finalizar orçamento");
            bool cancel = GuiButton((Rectangle){panel.x+panel.width-170, panel.y+456, 140, 36}, "Cancelar");

            if(ok){
                trim(g_seller);
                if(!g_seller[0]) snprintf(quoteStatus,sizeof(quoteStatus),"Informe o nome do vendedor.");
                else if(g_quoteCount==0) snprintf(quoteStatus,sizeof(quoteStatus),"Orçamento vazio.");
                else {
                    append_quote_csv(g_seller, g_quote, g_quoteCount);
                    save_csv(CSV_FILE); // marca dirty
#ifndef _WIN32
                    pthread_mutex_lock(&g_sockLock);
                    if(g_connected){
                        int pr = net_print_with_payload_locked(1);
                        if(pr!=0) snprintf(quoteStatus,sizeof(quoteStatus),"Sem impressão (Windows indisponível).");
                        else quoteStatus[0]=0;
                    } else {
                        snprintf(quoteStatus,sizeof(quoteStatus),"Sem impressão (desconectado).");
                    }
                    pthread_mutex_unlock(&g_sockLock);
#endif
                    quoteClear();
                    g_modal=MODAL_NONE; g_searchEdit=false;
                }
            }
            if(cancel){ g_modal=MODAL_NONE; g_searchEdit=false; quoteStatus[0]=0; }
            if(quoteStatus[0]) DrawText(quoteStatus, panel.x+360, panel.y+462, 20, COL_WARN);
        }

        EndDrawing();
    }

    save_csv(CSV_FILE);
    CloseWindow();

#ifndef _WIN32
    // fecha socket limpo
    pthread_mutex_lock(&g_sockLock); net_close_locked(); pthread_mutex_unlock(&g_sockLock);
#endif
    return 0;
}
