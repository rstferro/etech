/* =====================================================================
 * sales.c — Operações de vendas, orçamento, carrinho e relatório mensal.
 * ===================================================================== */

#include "platform.h"
#include "app_config.h"
#include "app_types.h"
#include "app_state.h"
#include "utils.h"
#include "sales.h"

/* ------------------------------------------------------------------ */
/* Helpers internos                                                     */
/* ------------------------------------------------------------------ */

static char *dupstr_simple(const char *src) {
    if (!src) src = "";
    size_t n = strlen(src) + 1;
    char  *out = (char *)malloc(n);
    if (out) memcpy(out, src, n);
    return out;
}

static int sales_line_exists(char **lines, int count, const char *line) {
    for (int i = 0; i < count; i++)
        if (strcmp(lines[i], line) == 0) return 1;
    return 0;
}

static int is_leap_year(int year) {
    return ((year % 4 == 0 && year % 100 != 0) || (year % 400 == 0));
}

static int days_in_month(int year, int month) {
    static const int days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (month == 2)              return days[1] + is_leap_year(year);
    if (month < 1 || month > 12) return 30;
    return days[month - 1];
}

static void current_date_parts(int *year, int *month, int *day) {
    time_t t = time(NULL);
    struct tm tmv;
    localtime_s(&tmv, &t);
    if (year)  *year  = tmv.tm_year + 1900;
    if (month) *month = tmv.tm_mon + 1;
    if (day)   *day   = tmv.tm_mday;
}

static int split_csv_cols(char *line, char **cols, int maxCols) {
    int n = 0;
    char *p = line;
    while (p && n < maxCols) {
        cols[n++] = p;
        char *comma = strchr(p, ',');
        if (!comma) break;
        *comma = 0;
        p = comma + 1;
    }
    return n;
}

static int find_item_by_sku(const char *sku) {
    for (int i = 0; i < g_count; i++)
        if (strcmp(g_items[i].sku, sku) == 0) return i;
    return -1;
}

static int find_sale_summary(SaleSummary *rows, int count,
                              const char *sku, const char *name) {
    for (int i = 0; i < count; i++)
        if (strcmp(rows[i].sku, sku) == 0 && strcmp(rows[i].name, name) == 0)
            return i;
    return -1;
}

static void load_month_sales(SaleSummary *rows, int *rowCount,
                              float *totalCost, float *totalSale,
                              float *totalDiscount, float *totalProfit,
                              int *year, int *month, int *dim) {
    int cy = 0, cm = 0, cd = 0;
    current_date_parts(&cy, &cm, &cd);
    (void)cd;
    int mdays = days_in_month(cy, cm);
    if (year)  *year  = cy;
    if (month) *month = cm;
    if (dim)   *dim   = mdays;

    *rowCount     = 0;
    *totalCost    = 0.0f;
    *totalSale    = 0.0f;
    *totalDiscount = 0.0f;
    *totalProfit  = 0.0f;

    FILE *f = fopen(SALES_FILE, "rb");
    if (!f) return;

    char line[4096];
    if (!fgets(line, sizeof(line), f)) { fclose(f); return; } /* pula cabeçalho */

    while (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\r\n")] = 0;
        if (!line[0]) continue;

        char buf[4096];
        strncpy(buf, line, sizeof(buf)-1); buf[sizeof(buf)-1] = 0;

        char *cols[16] = {0};
        int nt = split_csv_cols(buf, cols, 16);
        if (nt < 7) continue;

        int sy = 0, sm = 0, sd = 0;
        if (sscanf(cols[0], "%d-%d-%d", &sy, &sm, &sd) != 3) continue;
        if (sy != cy || sm != cm || sd < 1 || sd > mdays)    continue;

        const char *sku  = cols[2] ? cols[2] : "";
        const char *name = cols[3] ? cols[3] : "";
        int qty = atoi(cols[4] ? cols[4] : "0");
        if (qty <= 0) continue;

        float lineCost = 0.0f, lineGross = 0.0f;
        float lineDiscount = 0.0f, lineNet = 0.0f, lineProfit = 0.0f;

        if (nt >= 13) {
            /* Formato novo: datetime,seller,sku,name,qty,unit_cost,unit_price,
               line_cost,line_total,discount_pct,line_discount,line_net_total,line_profit */
            lineCost     = (float)strtod(cols[7]  ? cols[7]  : "0", NULL);
            lineGross    = (float)strtod(cols[8]  ? cols[8]  : "0", NULL);
            lineDiscount = (float)strtod(cols[10] ? cols[10] : "0", NULL);
            lineNet      = (float)strtod(cols[11] ? cols[11] : "0", NULL);
            lineProfit   = (float)strtod(cols[12] ? cols[12] : "0", NULL);
            (void)lineGross;
        } else {
            /* Formato antigo: datetime,seller,sku,name,qty,unit_price,line_total */
            float unitPrice = (float)strtod(cols[5] ? cols[5] : "0", NULL);
            lineGross = (float)strtod(cols[6] ? cols[6] : "0", NULL);
            if (lineGross <= 0.0f) lineGross = unitPrice * (float)qty;
            lineNet = lineGross;
            int itemIdx = find_item_by_sku(sku);
            float unitCost = (itemIdx >= 0) ? g_items[itemIdx].cost_price : 0.0f;
            lineCost   = unitCost * (float)qty;
            lineProfit = lineNet - lineCost;
        }

        int pos = find_sale_summary(rows, *rowCount, sku, name);
        if (pos < 0 && *rowCount < MAX_SALES_SUMMARY) {
            pos = (*rowCount)++;
            memset(&rows[pos], 0, sizeof(rows[pos]));
            strncpy(rows[pos].sku,  sku,  sizeof(rows[pos].sku)  - 1);
            strncpy(rows[pos].name, name, sizeof(rows[pos].name) - 1);
        }
        if (pos >= 0) {
            rows[pos].qty          += qty;
            rows[pos].totalCost    += lineCost;
            rows[pos].totalSale    += lineNet;
            rows[pos].totalDiscount += lineDiscount;
            rows[pos].profit       += lineProfit;
        }

        *totalCost     += lineCost;
        *totalSale     += lineNet;
        *totalDiscount += lineDiscount;
        *totalProfit   += lineProfit;
    }
    fclose(f);
}

/* ------------------------------------------------------------------ */
/* Funções exportadas — persistência                                    */
/* ------------------------------------------------------------------ */

void append_sales_csv(const char *seller, CartItem *cart, int cartCount, int discPct) {
    FILE *f = fopen(SALES_FILE, "ab");
    if (!f) return;
    fseek(f, 0, SEEK_END);
    if (ftell(f) == 0) {
        const char *hdr = "datetime,seller,sku,name,qty,unit_cost,unit_price,"
                          "line_cost,line_total,discount_pct,line_discount,"
                          "line_net_total,line_profit\n";
        fwrite(hdr, 1, strlen(hdr), f);
    }
    if (discPct < 0)   discPct = 0;
    if (discPct > 100) discPct = 100;
    time_t t = time(NULL);
    struct tm tmv; localtime_s(&tmv, &t);
    char dt[32]; strftime(dt, sizeof(dt), "%Y-%m-%d %H:%M:%S", &tmv);

    for (int i = 0; i < cartCount; i++) {
        Item  *it = &g_items[cart[i].itemIndex];
        float  lineCost     = it->cost_price * (float)cart[i].qty;
        float  lineTotal    = it->price      * (float)cart[i].qty;
        float  lineDiscount = lineTotal * ((float)discPct / 100.0f);
        float  lineNet      = lineTotal - lineDiscount;
        if (lineNet < 0.0f) lineNet = 0.0f;
        float  lineProfit   = lineNet - lineCost;
        char   line[2048];
        snprintf(line, sizeof(line),
                 "%s,%s,%s,%s,%d,%.2f,%.2f,%.2f,%.2f,%d,%.2f,%.2f,%.2f\n",
                 dt, seller, it->sku, it->name, cart[i].qty,
                 it->cost_price, it->price,
                 lineCost, lineTotal, discPct,
                 lineDiscount, lineNet, lineProfit);
        fwrite(line, 1, strlen(line), f);
    }
    fclose(f);
    g_salesNeedPush = 1;
}

void sales_file_to_text(char **outBuf, int *outLen) {
    EnterCriticalSection(&g_csvLock);
    FILE  *f   = fopen(SALES_FILE, "rb");
    size_t cap = 4096, pos = 0;
    char  *txt = (char *)malloc(cap);
    if (!txt) {
        LeaveCriticalSection(&g_csvLock);
        if (outBuf) *outBuf = NULL;
        if (outLen) *outLen = 0;
        return;
    }
    txt[0] = 0;
    if (f) {
        char line[4096];
        while (fgets(line, sizeof(line), f)) {
            size_t L = strlen(line);
            if (pos + L + 8 >= cap) { cap = (cap + L + 4096) * 2; txt = (char *)realloc(txt, cap); }
            memcpy(txt + pos, line, L); pos += L;
        }
        fclose(f);
    }
    if (pos == 0) {
        const char *hdr = "datetime,seller,sku,name,qty,unit_cost,unit_price,"
                          "line_cost,line_total,discount_pct,line_discount,"
                          "line_net_total,line_profit\n";
        size_t L = strlen(hdr);
        if (pos + L + 1 >= cap) { cap = L + 32; txt = (char *)realloc(txt, cap); }
        memcpy(txt + pos, hdr, L); pos += L;
    }
    txt[pos] = 0;
    LeaveCriticalSection(&g_csvLock);
    if (outBuf) *outBuf = txt;
    if (outLen) *outLen = (int)pos;
}

void merge_sales_text(const char *txt) {
    if (!txt || !*txt) return;
    EnterCriticalSection(&g_csvLock);

    char **lines = NULL;
    int    count = 0, cap = 0;
    char   cur[4096];

    FILE *f = fopen(SALES_FILE, "rb");
    if (f) {
        while (fgets(cur, sizeof(cur), f)) {
            cur[strcspn(cur, "\r\n")] = 0;
            if (!cur[0]) continue;
            if (cap <= count) {
                cap = cap ? cap*2 : 256;
                lines = (char **)realloc(lines, sizeof(char *) * (size_t)cap);
            }
            lines[count++] = dupstr_simple(cur);
        }
        fclose(f);
    }

    char *copy = dupstr_simple(txt);
    if (copy) {
        for (char *p = strtok(copy, "\n"); p; p = strtok(NULL, "\n")) {
            p[strcspn(p, "\r")] = 0;
            if (!p[0]) continue;
            if (starts_with_icase(p, "datetime,")) {
                if (count == 0) {
                    if (cap <= count) { cap = cap ? cap*2 : 256; lines = (char **)realloc(lines, sizeof(char *)*(size_t)cap); }
                    lines[count++] = dupstr_simple(p);
                }
                continue;
            }
            if (!sales_line_exists(lines, count, p)) {
                if (cap <= count) { cap = cap ? cap*2 : 256; lines = (char **)realloc(lines, sizeof(char *)*(size_t)cap); }
                lines[count++] = dupstr_simple(p);
            }
        }
        free(copy);
    }

    f = fopen(SALES_FILE, "wb");
    if (f) {
        if (count == 0 || !starts_with_icase(lines[0], "datetime,")) {
            const char *hdr = "datetime,seller,sku,name,qty,unit_cost,unit_price,"
                              "line_cost,line_total,discount_pct,line_discount,"
                              "line_net_total,line_profit\n";
            fwrite(hdr, 1, strlen(hdr), f);
        }
        for (int i = 0; i < count; i++) { fputs(lines[i], f); fputc('\n', f); }
        fclose(f);
    }
    for (int i = 0; i < count; i++) free(lines[i]);
    free(lines);
    LeaveCriticalSection(&g_csvLock);
}

void append_quote_csv(const char *seller, CartItem *cart, int cartCount) {
    FILE *f = fopen(QUOTE_FILE, "ab");
    if (!f) return;
    fseek(f, 0, SEEK_END);
    if (ftell(f) == 0) {
        const char *hdr = "datetime,seller,sku,name,qty,unit_price,line_total\n";
        fwrite(hdr, 1, strlen(hdr), f);
    }
    time_t t = time(NULL);
    struct tm tmv; localtime_s(&tmv, &t);
    char dt[32]; strftime(dt, sizeof(dt), "%Y-%m-%d %H:%M:%S", &tmv);

    for (int i = 0; i < cartCount; i++) {
        Item  *it = &g_items[cart[i].itemIndex];
        float  lineTotal = it->price * (float)cart[i].qty;
        char   line[2048];
        snprintf(line, sizeof(line), "%s,%s,%s,%s,%d,%.2f,%.2f\n",
                 dt, seller, it->sku, it->name, cart[i].qty,
                 it->price, lineTotal);
        fwrite(line, 1, strlen(line), f);
    }
    fclose(f);
}

/* ------------------------------------------------------------------ */
/* Relatório mensal (modal raygui)                                      */
/* ------------------------------------------------------------------ */

void DrawMonthlySalesModal(void) {
    SaleSummary rows[MAX_SALES_SUMMARY];
    int   rowCount = 0, year = 0, month = 0, dim = 0;
    float totalCost = 0.0f, totalSale = 0.0f, totalDiscount = 0.0f, totalProfit = 0.0f;

    load_month_sales(rows, &rowCount, &totalCost, &totalSale,
                     &totalDiscount, &totalProfit, &year, &month, &dim);

    Rectangle panel = {GetScreenWidth()/2.0f - 560,
                       GetScreenHeight()/2.0f - 310,
                       1120, 620};
    DrawRectangleRec(panel, COL_PANEL);
    DrawRectangleLinesEx(panel, 2, (Color){122, 61, 245, 255});

    DrawText(TextFormat("Vendas do mês atual - %02d/%04d (1 a %d)", month, year, dim),
             panel.x+16, panel.y+12, 24, COL_TEXT);
    DrawText(TextFormat("Custo: R$ %.2f    Venda líquida: R$ %.2f    Descontos: R$ %.2f    Lucro: R$ %.2f",
                         totalCost, totalSale, totalDiscount, totalProfit),
             panel.x+16, panel.y+46, 22,
             (totalProfit >= 0.0f) ? COL_TEXT : COL_WARN);

    Rectangle header = {panel.x+16, panel.y+84, panel.width-32, 32};
    DrawRectangleRec(header, COL_HEADER);

    int xSku    = (int)header.x +   8;
    int xName   = (int)header.x + 145;
    int xQty    = (int)header.x + 545;
    int xCost   = (int)header.x + 625;
    int xSale   = (int)header.x + 765;
    int xProfit = (int)header.x + 920;

    DrawText("SKU",        xSku,    header.y+7, 18, COL_TEXT);
    DrawText("Produto",    xName,   header.y+7, 18, COL_TEXT);
    DrawText("Qtd",        xQty,    header.y+7, 18, COL_TEXT);
    DrawText("Custo total",xCost,   header.y+7, 18, COL_TEXT);
    DrawText("Venda total",xSale,   header.y+7, 18, COL_TEXT);
    DrawText("Lucro",      xProfit, header.y+7, 18, COL_TEXT);

    Rectangle list = {panel.x+16, panel.y+116, panel.width-32, 420};
    DrawRectangleRec(list, (Color){24, 24, 24, 255});

    int rowH    = 30;
    int visible = (int)(list.height / rowH);
    if (g_salesScroll < 0)                      g_salesScroll = 0;
    if (g_salesScroll > rowCount - visible)      g_salesScroll = rowCount - visible;
    if (g_salesScroll < 0)                       g_salesScroll = 0;

    int wheel = GetMouseWheelMove();
    if (CheckCollisionPointRec(GetMousePosition(), list) && wheel != 0) {
        g_salesScroll -= wheel;
        if (g_salesScroll < 0)                   g_salesScroll = 0;
        if (g_salesScroll > rowCount - visible)  g_salesScroll = rowCount - visible;
        if (g_salesScroll < 0)                   g_salesScroll = 0;
    }

    for (int r = 0; r < visible; r++) {
        int idx = g_salesScroll + r;
        if (idx >= rowCount) break;
        int       y   = (int)(list.y + r * rowH);
        Rectangle row = {list.x, (float)y, list.width, (float)rowH};
        DrawRectangleRec(row, (r % 2 == 0) ? COL_ROW_A : COL_ROW_B);

        DrawTextClippedCell(rows[idx].sku,  (Rectangle){(float)xSku,  (float)y+7, 125, 22}, 18, COL_TEXT);
        DrawTextClippedCell(rows[idx].name, (Rectangle){(float)xName, (float)y+7, 385, 22}, 18, COL_TEXT);
        DrawText(TextFormat("%d",       rows[idx].qty),        xQty,    y+7, 18, COL_TEXT);
        DrawText(TextFormat("R$ %.2f",  rows[idx].totalCost),  xCost,   y+7, 18, COL_TEXT);
        DrawText(TextFormat("R$ %.2f",  rows[idx].totalSale),  xSale,   y+7, 18, COL_TEXT);
        DrawText(TextFormat("R$ %.2f",  rows[idx].profit),     xProfit, y+7, 18,
                 rows[idx].profit >= 0.0f ? COL_TEXT : COL_WARN);
    }

    if (rowCount == 0) {
        DrawText("Nenhuma venda registrada neste mês.", panel.x+32, panel.y+140, 22, COL_TEXT_DIM);
    } else {
        DrawText(TextFormat("Produtos diferentes vendidos: %d", rowCount),
                 panel.x+16, panel.y+548, 20, COL_TEXT_DIM);
    }

    if (GuiButton((Rectangle){panel.x + panel.width - 160,
                               panel.y + panel.height - 52, 130, 36}, "Fechar")) {
        g_modal      = MODAL_NONE;
        g_searchEdit = false;
    }
}

/* ------------------------------------------------------------------ */
/* CRUD do carrinho de vendas                                           */
/* ------------------------------------------------------------------ */

int cartFindItem(int itemIndex) {
    for (int i = 0; i < g_cartCount; i++)
        if (g_cart[i].itemIndex == itemIndex) return i;
    return -1;
}

void cartAdd(int itemIndex, int qty) {
    if (qty <= 0) return;
    int pos = cartFindItem(itemIndex);
    if (pos >= 0) g_cart[pos].qty += qty;
    else if (g_cartCount < MAX_CART) {
        g_cart[g_cartCount].itemIndex = itemIndex;
        g_cart[g_cartCount].qty       = qty;
        g_cartCount++;
    }
}

void cartRemoveAt(int pos) {
    if (pos < 0 || pos >= g_cartCount) return;
    for (int i = pos; i < g_cartCount - 1; i++) g_cart[i] = g_cart[i+1];
    g_cartCount--;
    if (cartSel >= g_cartCount) cartSel = g_cartCount - 1;
}

void cartClear(void) { g_cartCount = 0; cartSel = -1; }

/* ------------------------------------------------------------------ */
/* CRUD do orçamento                                                    */
/* ------------------------------------------------------------------ */

int quoteFindItem(int itemIndex) {
    for (int i = 0; i < g_quoteCount; i++)
        if (g_quote[i].itemIndex == itemIndex) return i;
    return -1;
}

void quoteAdd(int itemIndex, int qty) {
    if (qty <= 0) return;
    int pos = quoteFindItem(itemIndex);
    if (pos >= 0) g_quote[pos].qty += qty;
    else if (g_quoteCount < MAX_CART) {
        g_quote[g_quoteCount].itemIndex = itemIndex;
        g_quote[g_quoteCount].qty       = qty;
        g_quoteCount++;
    }
}

void quoteRemoveAt(int pos) {
    if (pos < 0 || pos >= g_quoteCount) return;
    for (int i = pos; i < g_quoteCount - 1; i++) g_quote[i] = g_quote[i+1];
    g_quoteCount--;
    if (quoteSel >= g_quoteCount) quoteSel = g_quoteCount - 1;
}

void quoteClear(void) { g_quoteCount = 0; quoteSel = -1; }
