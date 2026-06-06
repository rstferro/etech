/* =====================================================================
 * printer.c — Impressão ESC/POS via WinSpool + geração de cupons.
 * ===================================================================== */

#include "platform.h"
#include "app_config.h"
#include "app_types.h"
#include "app_state.h"
#include "utils.h"
#include "printer.h"

/* ------------------------------------------------------------------ */
/* Helpers internos                                                     */
/* ------------------------------------------------------------------ */

static int escpos_send_usb(const char *printerName, const unsigned char *buf, int len) {
    HANDLE hPrinter;
    if (!OpenPrinterA((LPSTR)printerName, &hPrinter, NULL)) return -10;
    DOC_INFO_1A di;
    di.pDocName    = (LPSTR)"E-TECH CUPOM";
    di.pOutputFile = NULL;
    di.pDatatype   = (LPSTR)"RAW";
    if (!StartDocPrinterA(hPrinter, 1, (LPBYTE)&di)) {
        ClosePrinter(hPrinter); return -11;
    }
    if (!StartPagePrinter(hPrinter)) {
        EndDocPrinter(hPrinter); ClosePrinter(hPrinter); return -12;
    }
    DWORD written = 0;
    BOOL  ok = WritePrinter(hPrinter, (LPVOID)buf, (DWORD)len, &written);
    EndPagePrinter(hPrinter);
    EndDocPrinter(hPrinter);
    ClosePrinter(hPrinter);
    if (!ok || (int)written != len) return -13;
    return 0;
}

static bool usb_check_printer(const char *printerName) {
    HANDLE h;
    if (OpenPrinterA((LPSTR)printerName, &h, NULL)) { ClosePrinter(h); return true; }
    return false;
}

static void prn_now_str(char *out, int outsz) {
    time_t t = time(NULL);
    struct tm lt;
    localtime_s(&lt, &t);
    strftime(out, outsz, "%d/%m/%Y %H:%M:%S", &lt);
}

static void buf_put(unsigned char **p, const void *src, int n) {
    memcpy(*p, src, n); *p += n;
}

/* Converte UTF-8 para CP850 antes de enviar RAW à impressora ESC/POS.
 * Isso garante que acentos (ç, á, é, etc.) saiam corretamente na Elgin. */
static int escpos_utf8_to_cp850(const char *src, unsigned char *dst, int dstCap) {
    if (!src || dstCap <= 0) return 0;
    int wlen = MultiByteToWideChar(CP_UTF8, 0, src, -1, NULL, 0);
    if (wlen <= 0) {
        int n = (int)strlen(src);
        if (n > dstCap) n = dstCap;
        memcpy(dst, src, n);
        return n;
    }
    wchar_t *wbuf = (wchar_t *)malloc((size_t)wlen * sizeof(wchar_t));
    if (!wbuf) return 0;
    MultiByteToWideChar(CP_UTF8, 0, src, -1, wbuf, wlen);
    int need = WideCharToMultiByte(850, 0, wbuf, -1, NULL, 0, NULL, NULL);
    if (need <= 0) { free(wbuf); return 0; }
    char *mb = (char *)malloc((size_t)need);
    if (!mb) { free(wbuf); return 0; }
    WideCharToMultiByte(850, 0, wbuf, -1, mb, need, NULL, NULL);
    int outLen = need - 1; /* sem o terminador nulo */
    if (outLen > dstCap) outLen = dstCap;
    memcpy(dst, mb, outLen);
    free(mb);
    free(wbuf);
    return outLen;
}

static int escpos_puts_bounded(unsigned char **p, unsigned char *end, const char *s) {
    unsigned char tmp[2048];
    int n = escpos_utf8_to_cp850(s, tmp, (int)sizeof(tmp));
    if (*p + n > end) return 0;
    memcpy(*p, tmp, n);
    *p += n;
    return 1;
}

static void buf_puts(unsigned char **p, const char *s) {
    unsigned char tmp[2048];
    int n = escpos_utf8_to_cp850(s, tmp, (int)sizeof(tmp));
    memcpy(*p, tmp, n);
    *p += n;
}

static void prn_print_center(unsigned char **p, const char *s, int width_chars) {
    (void)width_chars;
    buf_puts(p, s);
    buf_puts(p, "\n");
}

static void prn_hr(unsigned char **p, char ch, int width_chars) {
    for (int i = 0; i < width_chars; i++) { char c[2] = {ch, 0}; buf_puts(p, c); }
    buf_puts(p, "\n");
}

static void fmt_money(char *out, size_t outsz, float v) {
    snprintf(out, outsz, "%.2f", (double)v);
}

static int prn_cp850_len(const char *s) {
    unsigned char tmp[2048];
    return escpos_utf8_to_cp850(s ? s : "", tmp, (int)sizeof(tmp));
}

static int prn_utf8_char_bytes(const char *s) {
    unsigned char c = (unsigned char)s[0];
    if (c < 0x80) return 1;
    if ((c & 0xE0) == 0xC0) return 2;
    if ((c & 0xF0) == 0xE0) return 3;
    if ((c & 0xF8) == 0xF0) return 4;
    return 1;
}

static const char *prn_copy_cols(char *dst, size_t dstsz,
                                  const char *src, int maxCols) {
    if (!dst || dstsz == 0) return src ? src : "";
    dst[0] = 0;
    if (!src) return "";
    int    used = 0;
    size_t pos  = 0;
    const char *p = src;
    while (*p) {
        int  cb = prn_utf8_char_bytes(p);
        if (cb <= 0) cb = 1;
        char one[8] = {0};
        for (int i = 0; i < cb && p[i]; i++) one[i] = p[i];
        int w = prn_cp850_len(one);
        if (w <= 0) w = 1;
        if (used + w > maxCols) break;
        if (pos + (size_t)cb >= dstsz) break;
        memcpy(dst + pos, p, (size_t)cb);
        pos  += (size_t)cb;
        used += w;
        p    += cb;
    }
    dst[pos] = 0;
    return p;
}

static void prn_safe_sku8(char *dst, size_t dstsz, const char *sku) {
    if (!dst || dstsz == 0) return;
    snprintf(dst, dstsz, "%.8s", sku ? sku : "");
}

static void prn_make_receipt_line(char *out, size_t outsz,
                                   const char *sku, const char *name,
                                   const char *qty, const char *unit,
                                   int namew, const char **nameRest) {
    char sku8[16];
    char namePart[256];
    prn_safe_sku8(sku8, sizeof(sku8), sku);
    const char *rest = prn_copy_cols(namePart, sizeof(namePart),
                                     name ? name : "", namew);
    int nameLen = prn_cp850_len(namePart);
    int pad = namew - nameLen;
    if (pad < 0) pad = 0;
    snprintf(out, outsz, "%-8s %s%*s %3s %9s\n",
             sku8, namePart, pad, "",
             qty  ? qty  : "",
             unit ? unit : "");
    if (nameRest) *nameRest = rest;
}

/* Constrói o buffer binário ESC/POS de um cupom completo.
 * Retorna o número de bytes escritos, ou negativo em erro. */
static int build_receipt(unsigned char *out, int outsz,
                          const char *store, const char *seller,
                          CartItem *cart, int cartCount,
                          float subtotal, int discountPct, int is_quote) {
    unsigned char *p = out, *end = out + outsz;

#define PUT(b, n)  do { if (p + (n) > end) return -1; buf_put(&p, (b), (n)); } while(0)
#define PUTS(s)    do { if (!escpos_puts_bounded(&p, end, (s))) return -1; } while(0)

    const unsigned char init[]     = {0x1B, 0x40};
    const unsigned char align_l[]  = {0x1B, 0x61, 0x00};
    const unsigned char align_c[]  = {0x1B, 0x61, 0x01};
    const unsigned char bold_on[]  = {0x1B, 0x45, 0x01};
    const unsigned char bold_off[] = {0x1B, 0x45, 0x00};
    const unsigned char dbl_on[]   = {0x1D, 0x21, 0x11};
    const unsigned char dbl_off[]  = {0x1D, 0x21, 0x00};
    const unsigned char cut_full[] = {0x1D, 0x56, 0x00};
    unsigned char cp_sel[] = {0x1B, 0x74, 0x00};
    cp_sel[2] = PRN_CODEPAGE;

    PUT(init, sizeof(init)); PUT(cp_sel, sizeof(cp_sel));
    PUT(align_c, sizeof(align_c));
    PUT(bold_on, sizeof(bold_on)); PUT(dbl_on, sizeof(dbl_on));
    prn_print_center(&p, (store && *store) ? store : "E-TECH", PRN_SAFE_WIDTH);
    PUT(dbl_off, sizeof(dbl_off)); PUT(bold_off, sizeof(bold_off));
    prn_print_center(&p, "Componentes e Conserto", PRN_SAFE_WIDTH);

    if (is_quote) {
        PUT(bold_on, sizeof(bold_on));
        prn_print_center(&p, "ORÇAMENTO", PRN_SAFE_WIDTH);
        PUT(bold_off, sizeof(bold_off));
        prn_print_center(&p, "NÃO VÁLIDO COMO DOCUMENTO FISCAL", PRN_SAFE_WIDTH);
    }

    char d[64]; prn_now_str(d, sizeof(d));
    prn_print_center(&p, d, PRN_SAFE_WIDTH);
    prn_hr(&p, '-', PRN_SAFE_WIDTH);
    PUT(align_l, sizeof(align_l));

    {
        int namew = PRN_SAFE_WIDTH - 8 - 1 - 3 - 1 - 9;
        if (namew < 8) namew = 8;
        char head[160];
        prn_make_receipt_line(head, sizeof(head),
                              "SKU", "DESCRIÇÃO", "QTD", "VL.UNIT", namew, NULL);
        PUTS(head);
    }
    prn_hr(&p, '-', PRN_SAFE_WIDTH);

    for (int i = 0; i < cartCount; i++) {
        Item *it  = &g_items[cart[i].itemIndex];
        int   qty = cart[i].qty;
        int   namew = PRN_SAFE_WIDTH - 8 - 1 - 3 - 1 - 9;
        if (namew < 8) namew = 8;
        char qtyStr[16], unitStr[16], ln1[512], ln2[512];
        snprintf(qtyStr, sizeof(qtyStr), "%d", qty);
        fmt_money(unitStr, sizeof(unitStr), it->price);
        const char *rest = NULL;
        prn_make_receipt_line(ln1, sizeof(ln1), it->sku, it->name,
                              qtyStr, unitStr, namew, &rest);
        PUTS(ln1);
        if (rest && *rest) {
            prn_make_receipt_line(ln2, sizeof(ln2), "", rest, "", "", namew, NULL);
            PUTS(ln2);
        }
    }
    prn_hr(&p, '-', PRN_SAFE_WIDTH);

    {
        float disc  = (discountPct > 0) ? (subtotal * (discountPct / 100.0f)) : 0.0f;
        float total = subtotal - disc;
        if (total < 0) total = 0;
        char subStr[16], discStr[16], totStr[16], ln[200];
        fmt_money(subStr, sizeof(subStr), subtotal);
        fmt_money(discStr, sizeof(discStr), disc);
        fmt_money(totStr,  sizeof(totStr),  total);
        snprintf(ln, sizeof(ln), "%*s %9s\n",
                 PRN_SAFE_WIDTH - 9,
                 is_quote ? "SUBTOTAL ORÇ:" : "SUBTOTAL:",
                 subStr);
        PUTS(ln);
        if (discountPct > 0) {
            char descLab[64];
            snprintf(descLab, sizeof(descLab), "DESCONTO (%d%%):", discountPct);
            snprintf(ln, sizeof(ln), "%*s %9s\n", PRN_SAFE_WIDTH - 9, descLab, discStr);
            PUTS(ln);
        }
        PUT(bold_on, sizeof(bold_on));
        snprintf(ln, sizeof(ln), "%*s %9s\n",
                 PRN_SAFE_WIDTH - 9,
                 is_quote ? "TOTAL ORÇ:" : "TOTAL:",
                 totStr);
        PUTS(ln);
        PUT(bold_off, sizeof(bold_off));
    }

    if (seller && *seller) {
        char ln[200];
        snprintf(ln, sizeof(ln), "Vendedor: %s\n", seller);
        PUTS(ln);
    }
    if (is_quote) PUTS("Validade do preço sujeita a alterações.\n");
    PUTS("\nObrigado pela preferência!\n\n");
    PUT(cut_full, sizeof(cut_full));
    return (int)(p - out);

#undef PUT
#undef PUTS
}

/* ------------------------------------------------------------------ */
/* Funções exportadas                                                   */
/* ------------------------------------------------------------------ */

void UpdatePrinterStatus(void) {
    double now = GetTime();
    if (now - g_prnLastCheck < g_prnInterval) return;
    g_prnLastCheck  = now;
    g_prnConnected  = usb_check_printer(PRN_NAME);
}

int PrintCurrentCartReceipt(const char *storeTitle) {
    if (g_cartCount <= 0) return -1;
    float subtotal = 0.0f;
    for (int i = 0; i < g_cartCount; i++)
        subtotal += g_items[g_cart[i].itemIndex].price * (float)g_cart[i].qty;

    unsigned char buf[8192];
    int len = build_receipt(buf, sizeof(buf),
                            storeTitle ? storeTitle : "E-TECH",
                            g_seller, g_cart, g_cartCount,
                            subtotal, g_cartDiscPct, 0);
    if (len < 0) return -2;
    int rc = escpos_send_usb(PRN_NAME, buf, len);
    g_prnConnected = (rc == 0);
    g_prnLastCheck = GetTime();
    return rc;
}

int PrintCurrentQuoteReceipt(const char *storeTitle) {
    if (g_quoteCount <= 0) return -1;
    float subtotal = 0.0f;
    for (int i = 0; i < g_quoteCount; i++)
        subtotal += g_items[g_quote[i].itemIndex].price * (float)g_quote[i].qty;

    unsigned char buf[8192];
    int len = build_receipt(buf, sizeof(buf),
                            storeTitle ? storeTitle : "E-TECH",
                            g_seller, g_quote, g_quoteCount,
                            subtotal, g_quoteDiscPct, 1);
    if (len < 0) return -2;
    int rc = escpos_send_usb(PRN_NAME, buf, len);
    g_prnConnected = (rc == 0);
    g_prnLastCheck = GetTime();
    return rc;
}

int PrintPayloadReceiptDirect(const char *storeTitle, const char *payload, int is_quote) {
    typedef struct { char sku[64], name[128]; int qty; float price; } PItem;
    PItem  tmp[512];
    int    tcount  = 0;
    char   seller[64] = "";
    int    discPct = 0;
    float  subtotal = 0.0f;

    /* Salva payload para debug */
    if (payload) {
        FILE *df = fopen("last_print_payload.txt", "wb");
        if (df) { fwrite(payload, 1, strlen(payload), df); fclose(df); }
    }

    const char *p = payload ? payload : "";
    while (*p) {
        const char *nl = strchr(p, '\n');
        int n = nl ? (int)(nl - p) : (int)strlen(p);
        if (n <= 0) break;
        char lineRaw[1024];
        if (n > (int)sizeof(lineRaw) - 1) n = (int)sizeof(lineRaw) - 1;
        memcpy(lineRaw, p, n); lineRaw[n] = 0;
        trim_ws_inplace(lineRaw);

        if (lineRaw[0] == 0) { p = nl ? (nl+1) : (p+n); continue; }

        if (icase_starts_with(lineRaw, "SELLER:") || icase_starts_with(lineRaw, "SELLER=")) {
            const char *v = lineRaw + 7;
            while (*v == ':' || *v == '=' || *v == ' ' || *v == '\t') v++;
            strncpy(seller, v, sizeof(seller)-1);
            seller[sizeof(seller)-1] = 0;
            trim_ws_inplace(seller);

        } else if (icase_starts_with(lineRaw, "DISCOUNT:") || icase_starts_with(lineRaw, "DISCOUNT=")) {
            const char *v = lineRaw + 9;
            while (*v == ':' || *v == '=' || *v == ' ' || *v == '\t') v++;
            discPct = atoi(v);
            if (discPct < 0)   discPct = 0;
            if (discPct > 100) discPct = 100;

        } else {
            const char *q = lineRaw;
            if (icase_starts_with(lineRaw, "ITEM")) {
                q = lineRaw + 4;
                while (*q == ':' || *q == '=' || *q == ' ' || *q == '\t') q++;
                if (*q == 0) { p = nl ? (nl+1) : (p+n); continue; }
            }
            char buf2[1024];
            char *f1 = NULL, *f2 = NULL, *f3 = NULL, *f4 = NULL;
            int ok = 0;
            strncpy(buf2, q, sizeof(buf2)-1); buf2[sizeof(buf2)-1] = 0;
            ok = split4_any(buf2, "|\t", &f1, &f2, &f3, &f4);
            if (!ok) { strncpy(buf2, q, sizeof(buf2)-1); buf2[sizeof(buf2)-1] = 0;
                       ok = split4_any(buf2, ";",  &f1, &f2, &f3, &f4); }
            if (!ok) { strncpy(buf2, q, sizeof(buf2)-1); buf2[sizeof(buf2)-1] = 0;
                       ok = split4_any(buf2, ",",  &f1, &f2, &f3, &f4); }
            if (ok) {
                trim_ws_inplace(f1); trim_ws_inplace(f2);
                trim_ws_inplace(f3); trim_ws_inplace(f4);
                char *endq = NULL, *endp2 = NULL;
                long   qv = strtol(f3, &endq, 10);
                double pv = strtod(f4, &endp2);
                int qty_ok = (endq  && (*endq  == 0 || isspace((unsigned char)*endq)));
                int prv_ok = (endp2 && (*endp2 == 0 || isspace((unsigned char)*endp2)));
                if (qty_ok && prv_ok && qv >= 0 && tcount < 512) {
                    strncpy(tmp[tcount].sku,  f1, sizeof(tmp[tcount].sku) - 1);
                    strncpy(tmp[tcount].name, f2, sizeof(tmp[tcount].name) - 1);
                    tmp[tcount].sku [sizeof(tmp[tcount].sku) - 1]  = 0;
                    tmp[tcount].name[sizeof(tmp[tcount].name) - 1] = 0;
                    tmp[tcount].qty   = (int)qv;
                    tmp[tcount].price = (float)pv;
                    subtotal += tmp[tcount].price * (float)tmp[tcount].qty;
                    tcount++;
                }
            }
        }
        p = nl ? (nl+1) : (p+n);
    }

    /* Payload sem itens: imprime mensagem de aviso */
    if (tcount == 0) {
        unsigned char dbg[2048], *bp = dbg, *end = dbg + sizeof(dbg);
#define PUTD(b,n)  do{ if(bp+(n)>end) return -3; memcpy(bp,(b),(n)); bp+=(n); }while(0)
#define PUTSD(s)   do{ if(!escpos_puts_bounded(&bp,end,(s))) return -3; }while(0)
        const unsigned char init[]     = {0x1B, 0x40};
        const unsigned char align_c[]  = {0x1B, 0x61, 0x01};
        const unsigned char bold_on[]  = {0x1B, 0x45, 0x01};
        const unsigned char bold_off[] = {0x1B, 0x45, 0x00};
        const unsigned char cut_full[] = {0x1D, 0x56, 0x00};
        unsigned char cp_sel[] = {0x1B, 0x74, 0x00}; cp_sel[2] = PRN_CODEPAGE;
        PUTD(init, sizeof(init)); PUTD(cp_sel, sizeof(cp_sel));
        PUTD(align_c, sizeof(align_c)); PUTD(bold_on, sizeof(bold_on));
        PUTSD("AVISO: Payload recebido sem itens\n");
        PUTD(bold_off, sizeof(bold_off));
        PUTSD("Verifique o formato enviado pelo Linux.\n\n");
        PUTD(cut_full, sizeof(cut_full));
        int rcDbg = escpos_send_usb(PRN_NAME, dbg, (int)(bp - dbg));
        g_prnConnected = (rcDbg == 0); g_prnLastCheck = GetTime();
        return rcDbg;
#undef PUTD
#undef PUTSD
    }

    /* Montagem do cupom normal */
    unsigned char buf[8192], *bp = buf, *end = buf + sizeof(buf);
#define PUT2(b,n)  do{ if(bp+(n)>end) return -2; memcpy(bp,(b),(n)); bp+=(n); }while(0)
#define PUTS2(s)   do{ if(!escpos_puts_bounded(&bp,end,(s))) return -2; }while(0)
    const unsigned char init[]     = {0x1B, 0x40};
    const unsigned char align_l[]  = {0x1B, 0x61, 0x00};
    const unsigned char align_c[]  = {0x1B, 0x61, 0x01};
    const unsigned char bold_on[]  = {0x1B, 0x45, 0x01};
    const unsigned char bold_off[] = {0x1B, 0x45, 0x00};
    const unsigned char dbl_on[]   = {0x1D, 0x21, 0x11};
    const unsigned char dbl_off[]  = {0x1D, 0x21, 0x00};
    const unsigned char cut_full[] = {0x1D, 0x56, 0x00};
    unsigned char cp_sel[] = {0x1B, 0x74, 0x00}; cp_sel[2] = PRN_CODEPAGE;

    PUT2(init, sizeof(init)); PUT2(cp_sel, sizeof(cp_sel));
    PUT2(align_c, sizeof(align_c));
    PUT2(bold_on, sizeof(bold_on)); PUT2(dbl_on, sizeof(dbl_on));
    PUTS2(storeTitle && *storeTitle ? storeTitle : "E-TECH"); PUTS2("\n");
    PUT2(dbl_off, sizeof(dbl_off)); PUT2(bold_off, sizeof(bold_off));
    PUTS2("Componentes e Conserto\n");
    if (is_quote) {
        PUT2(bold_on, sizeof(bold_on)); PUTS2("ORÇAMENTO\n"); PUT2(bold_off, sizeof(bold_off));
        PUTS2("NÃO VÁLIDO COMO DOCUMENTO FISCAL\n");
    }
    { char d[64]; time_t tt = time(NULL); struct tm lt; localtime_s(&lt, &tt);
      strftime(d, sizeof(d), "%d/%m/%Y %H:%M:%S", &lt); PUTS2(d); PUTS2("\n"); }

    PUT2(align_l, sizeof(align_l));
    for (int i = 0; i < PRN_SAFE_WIDTH; i++) { PUTS2("-"); } PUTS2("\n");

    int namew = PRN_SAFE_WIDTH - 8 - 1 - 3 - 1 - 9;
    if (namew < 8) namew = 8;
    { char head[160]; prn_make_receipt_line(head, sizeof(head),
                                             "SKU","DESCRIÇÃO","QTD","VL.UNIT", namew, NULL);
      PUTS2(head); }
    for (int i = 0; i < PRN_SAFE_WIDTH; i++) { PUTS2("-"); } PUTS2("\n");

    for (int i = 0; i < tcount; i++) {
        char qtyStr[16], unitStr[16], ln1[512], ln2[512];
        snprintf(qtyStr, sizeof(qtyStr), "%d", tmp[i].qty);
        snprintf(unitStr, sizeof(unitStr), "%.2f", (double)tmp[i].price);
        const char *rest = NULL;
        prn_make_receipt_line(ln1, sizeof(ln1), tmp[i].sku, tmp[i].name,
                              qtyStr, unitStr, namew, &rest);
        PUTS2(ln1);
        if (rest && *rest) {
            prn_make_receipt_line(ln2, sizeof(ln2), "", rest, "", "", namew, NULL);
            PUTS2(ln2);
        }
    }
    for (int i = 0; i < PRN_SAFE_WIDTH; i++) { PUTS2("-"); } PUTS2("\n");

    float disc  = (discPct > 0) ? (subtotal * (discPct / 100.0f)) : 0.0f;
    float total = subtotal - disc;
    if (total < 0) total = 0;
    char subStr[16], discStr[16], totStr[16], ln[200];
    snprintf(subStr,  sizeof(subStr),  "%.2f", (double)subtotal);
    snprintf(discStr, sizeof(discStr), "%.2f", (double)disc);
    snprintf(totStr,  sizeof(totStr),  "%.2f", (double)total);
    snprintf(ln, sizeof(ln), "%*s %9s\n",
             PRN_SAFE_WIDTH - 9, is_quote ? "SUBTOTAL ORÇ:" : "SUBTOTAL:", subStr);
    PUTS2(ln);
    if (discPct > 0) {
        char descLab[64];
        snprintf(descLab, sizeof(descLab), "DESCONTO (%d%%):", discPct);
        snprintf(ln, sizeof(ln), "%*s %9s\n", PRN_SAFE_WIDTH - 9, descLab, discStr);
        PUTS2(ln);
    }
    PUT2(bold_on, sizeof(bold_on));
    snprintf(ln, sizeof(ln), "%*s %9s\n",
             PRN_SAFE_WIDTH - 9, is_quote ? "TOTAL ORÇ:" : "TOTAL:", totStr);
    PUTS2(ln);
    PUT2(bold_off, sizeof(bold_off));
    if (seller[0]) { snprintf(ln, sizeof(ln), "Vendedor: %s\n", seller); PUTS2(ln); }
    if (is_quote)  { PUTS2("Validade do preço sujeita a alterações.\n"); }
    PUTS2("\nObrigado pela preferência!\n\n");
    PUT2(cut_full, sizeof(cut_full));

    int rc = escpos_send_usb(PRN_NAME, buf, (int)(bp - buf));
    g_prnConnected = (rc == 0);
    g_prnLastCheck = GetTime();
    return rc;
#undef PUT2
#undef PUTS2
}
