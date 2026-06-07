/* =====================================================================
 * csv_io.c — Leitura/escrita de CSV e operações CRUD no estoque.
 * ===================================================================== */

#include "platform.h"
#include "app_config.h"
#include "app_types.h"
#include "app_state.h"
#include "utils.h"
#include "audit.h"
#include "undo.h"
#include "csv_io.h"

/* ------------------------------------------------------------------ */
/* Helpers internos                                                     */
/* ------------------------------------------------------------------ */

static void to_csv_line(char *out, size_t outsz, const Item *it) {
    snprintf(out, outsz, "%.*s,%.*s,%.*s,%d,%.2f,%.2f\n",
             63,  it->sku,
             127, it->name,
             63,  it->location,
             it->qty, it->price, it->cost_price);
}

static int from_csv_line(const char *line, Item *it) {
    char buf[2048];
    strncpy(buf, line, sizeof(buf)-1);
    buf[sizeof(buf)-1] = 0;

    char *tok[8] = {0};
    int   nt = 0;
    for (char *p = strtok(buf, ","); p && nt < 8; p = strtok(NULL, ","))
        tok[nt++] = p;
    if (nt < 4) return 0;

    strncpy(it->sku,      tok[0] ? tok[0] : "", 63);  it->sku[63]  = 0;
    strncpy(it->name,     tok[1] ? tok[1] : "", 127); it->name[127] = 0;
    strncpy(it->location, tok[2] ? tok[2] : "", 63);  it->location[63] = 0;
    trim(it->sku); trim(it->name); trim(it->location);

    it->qty        = atoi(tok[3] ? tok[3] : "0");
    it->price      = (nt >= 5 && tok[4]) ? (float)strtod(tok[4], NULL) : 0.0f;
    it->cost_price = (nt >= 6 && tok[5]) ? (float)strtod(tok[5], NULL) : 0.0f;
    return 1;
}

static long long make_sync_stamp(void) {
    long long st = (long long)time(NULL) * 1000LL
                 + ((long long)clock() * 1000LL / CLOCKS_PER_SEC) % 1000LL;
    if (st <= 0) st = 1;
    return st;
}

static void touch_csv_stamp(void) {
    long long st = make_sync_stamp();
    if (st <= g_csvStamp) st = g_csvStamp + 1;
    g_csvStamp = st;
}

/* ------------------------------------------------------------------ */
/* Funções exportadas                                                   */
/* ------------------------------------------------------------------ */

int save_csv(const char *path) {
    if (!g_savingRemoteCsv) touch_csv_stamp();
    EnterCriticalSection(&g_csvLock);
    FILE *f = fopen(path, "wb");
    if (!f) { LeaveCriticalSection(&g_csvLock); return 0; }
    fputs("sku,name,location,qty,price,cost_price\n", f);
    char line[2048];
    for (int i = 0; i < g_count; i++) {
        to_csv_line(line, sizeof(line), &g_items[i]);
        fputs(line, f);
    }
    fclose(f);
    LeaveCriticalSection(&g_csvLock);
    return 1;
}

int load_csv(const char *path) {
    EnterCriticalSection(&g_csvLock);
    FILE *f = fopen(path, "rb");
    if (!f) { LeaveCriticalSection(&g_csvLock); return 0; }
    g_count = 0;
    char line[2048];
    if (!fgets(line, sizeof(line), f)) {
        fclose(f);
        LeaveCriticalSection(&g_csvLock);
        return 0;
    }
    while (fgets(line, sizeof(line), f) && g_count < MAX_ITEMS) {
        Item it;
        if (from_csv_line(line, &it)) g_items[g_count++] = it;
    }
    fclose(f);
    LeaveCriticalSection(&g_csvLock);
    undo_clear();   /* estado foi substituído; histórico não é mais válido */
    return 1;
}

void csv_to_text(char **outBuf, int *outLen) {
    EnterCriticalSection(&g_csvLock);
    size_t cap = 1024 + (size_t)g_count * 160;
    char  *txt = (char *)malloc(cap);
    size_t pos = 0;
    pos += snprintf(txt + pos, cap - pos,
                    "#STAMP:%lld\nsku,name,location,qty,price,cost_price\n",
                    (long long)g_csvStamp);
    for (int i = 0; i < g_count; i++) {
        char line[2048];
        to_csv_line(line, sizeof(line), &g_items[i]);
        size_t L = strlen(line);
        if (pos + L + 8 >= cap) { cap *= 2; txt = (char *)realloc(txt, cap); }
        memcpy(txt + pos, line, L);
        pos += L;
    }
    LeaveCriticalSection(&g_csvLock);
    *outBuf = txt;
    *outLen = (int)pos;
}

void apply_csv_text(const char *txt) {
    EnterCriticalSection(&g_csvLock);
    Item      tmp[MAX_ITEMS];
    int       cnt = 0;
    const char *p = txt;
    long long  remoteStamp = 0;

    /* Lê cabeçalhos opcionais: #STAMP e a linha de colunas */
    for (;;) {
        char       firstLine[2048] = {0};
        const char *nl = strchr(p, '\n');
        int n = nl ? (int)(nl - p) : (int)strlen(p);
        if (n <= 0) break;
        if (n >= (int)sizeof(firstLine)) n = (int)sizeof(firstLine) - 1;
        memcpy(firstLine, p, n);
        firstLine[n] = 0;
        const char *next = nl ? (nl + 1) : (p + n);

        if (strncmp(firstLine, "#STAMP:", 7) == 0) {
            remoteStamp = atoll(firstLine + 7);
            p = next; continue;
        }
        if (strncmp(firstLine, "sku,", 4) == 0) { p = next; break; }
        break; /* linha de dados: começa a processar */
    }

    /* Descarta pacote mais antigo que o estado atual */
    if (g_csvStamp > 0 && remoteStamp < g_csvStamp) {
        LeaveCriticalSection(&g_csvLock);
        return;
    }

    while (*p && cnt < MAX_ITEMS) {
        const char *nl2 = strchr(p, '\n');
        int n2 = nl2 ? (int)(nl2 - p) : (int)strlen(p);
        if (n2 <= 0) break;
        char line[2048];
        if (n2 > (int)sizeof(line) - 1) n2 = (int)sizeof(line) - 1;
        memcpy(line, p, n2); line[n2] = 0;
        Item it;
        if (from_csv_line(line, &it)) tmp[cnt++] = it;
        p = nl2 ? (nl2 + 1) : (p + n2);
    }
    g_count = cnt;
    for (int i = 0; i < cnt; i++) g_items[i] = tmp[i];
    if (remoteStamp > g_csvStamp) g_csvStamp = remoteStamp;
    LeaveCriticalSection(&g_csvLock);

    g_savingRemoteCsv = 1;
    save_csv(CSV_FILE);
    g_savingRemoteCsv = 0;
    undo_clear();   /* CSV remoto substituiu o estado local */
}

/* ------------------------------------------------------------------ */
/* Ordenação da tabela                                                  */
/* ------------------------------------------------------------------ */

static int sort_compare(const void *a, const void *b) {
    int ia = *(const int *)a;
    int ib = *(const int *)b;
    if (ia < 0 || ia >= g_count || ib < 0 || ib >= g_count) return 0;

    const Item *pa = &g_items[ia];
    const Item *pb = &g_items[ib];
    int cmp = 0;

    switch (g_sortCol) {
        case SORT_COL_SKU:
            cmp = strcmp(pa->sku, pb->sku); break;
        case SORT_COL_NAME:
            cmp = strcmp(pa->name, pb->name); break;
        case SORT_COL_LOCATION:
            cmp = strcmp(pa->location, pb->location); break;
        case SORT_COL_QTY:
            cmp = (pa->qty > pb->qty) - (pa->qty < pb->qty); break;
        case SORT_COL_PRICE:
            cmp = (pa->price > pb->price) - (pa->price < pb->price); break;
        case SORT_COL_COST:
            cmp = (pa->cost_price > pb->cost_price) - (pa->cost_price < pb->cost_price); break;
        default: return 0;
    }
    return g_sortAsc ? cmp : -cmp;
}

void apply_sort(void) {
    if (g_sortCol == SORT_COL_NONE || filtered_count <= 1) return;
    qsort(filtered_indices, (size_t)filtered_count, sizeof(int), sort_compare);
}

/* ------------------------------------------------------------------ */
/* Filtro / CRUD                                                        */
/* ------------------------------------------------------------------ */

void rebuild_filter(void) {
    filtered_count = 0;
    for (int i = 0; i < g_count; i++) {
        if (!g_search[0]
            || starts_with_icase(g_items[i].sku,      g_search)
            || contains_icase(g_items[i].name,         g_search)
            || contains_icase(g_items[i].location,     g_search))
        {
            filtered_indices[filtered_count++] = i;
        }
    }
    if (g_selected >= filtered_count) g_selected = filtered_count - 1;
    if (filtered_count <= 0)          g_scrollIndex = 0;
    apply_sort(); /* mantém a ordenação ativa após filtrar */
}

void add_item(const Item *it) {
    EnterCriticalSection(&g_csvLock);
    if (g_count < MAX_ITEMS) g_items[g_count++] = *it;
    LeaveCriticalSection(&g_csvLock);
    audit_item_add(it);
    undo_push_add(it);
}

void update_item(int idx, const Item *it) {
    Item   before = {0};
    bool   valid  = false;

    EnterCriticalSection(&g_csvLock);
    if (idx >= 0 && idx < g_count) {
        before        = g_items[idx]; /* captura o estado ANTES de sobrescrever */
        g_items[idx]  = *it;
        valid         = true;
    }
    LeaveCriticalSection(&g_csvLock);

    if (valid) {
        audit_item_edit(&before, it);
        undo_push_edit(idx, &before);
    }
}

void delete_item(int idx) {
    Item deleted = {0};
    bool valid   = false;

    EnterCriticalSection(&g_csvLock);
    if (idx >= 0 && idx < g_count) {
        deleted = g_items[idx]; /* captura o item ANTES de remover */
        for (int i = idx; i < g_count - 1; i++) g_items[i] = g_items[i+1];
        g_count--;
        g_selected = -1;
        valid = true;
    }
    LeaveCriticalSection(&g_csvLock);

    if (valid) {
        audit_item_delete(&deleted);
        undo_push_delete(idx, &deleted);
    }
}

void GerarProximoSKU(char *buffer, int bufferSize) {
    int max_sku = 0;
    for (int i = 0; i < g_count; i++) {
        int current_sku = atoi(g_items[i].sku);
        if (current_sku > max_sku)
            max_sku = current_sku;
    }
    snprintf(buffer, bufferSize, "%05d", max_sku + 1);
}
