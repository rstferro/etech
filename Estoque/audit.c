/* =====================================================================
 * audit.c — Log de auditoria de operações do estoque.
 *
 * Formato de cada linha:
 *   YYYY-MM-DD HH:MM:SS | OPERACAO | campo=valor | campo=valor ...
 *
 * Exemplo:
 *   2026-06-06 14:32:01 | ADIÇÃO   | SKU=00358 | Nome=Resistor 10k | Qtd=100 | Preco=0.50
 *   2026-06-06 14:35:12 | EDIÇÃO   | SKU=00358 | ANTES=[Qtd=100 Preco=0.50] | DEPOIS=[Qtd=95 Preco=0.55]
 *   2026-06-06 14:40:05 | EXCLUSÃO | SKU=00358 | Nome=Resistor 10k
 *   2026-06-06 15:00:00 | VENDA    | Vendedor=João | Desc=5% | Total=R$42.75 | Itens=[00101 x2, 00358 x1]
 *   2026-06-06 15:30:00 | ORCAM.   | Vendedor=Maria | Total=R$80.00 | Itens=[00203 x5]
 * ===================================================================== */

#include "platform.h"
#include "app_config.h"
#include "app_types.h"
#include "app_state.h"
#include "utils.h"
#include "ui_helpers.h"
#include "audit.h"

/* ------------------------------------------------------------------ */
/* Estado interno                                                       */
/* ------------------------------------------------------------------ */

static CRITICAL_SECTION s_lock;

/* ------------------------------------------------------------------ */
/* Helpers internos                                                     */
/* ------------------------------------------------------------------ */

static void now_str(char *out, int outsz) {
    time_t t = time(NULL);
    struct tm tmv;
    localtime_s(&tmv, &t);
    strftime(out, outsz, "%Y-%m-%d %H:%M:%S", &tmv);
}

static void audit_write(const char *line) {
    EnterCriticalSection(&s_lock);
    FILE *f = fopen(AUDIT_FILE, "ab");
    if (f) {
        fputs(line, f);
        fputc('\n', f);
        fclose(f);
    }
    LeaveCriticalSection(&s_lock);
}

/* Serializa uma lista de itens do carrinho como "SKU x Qtd, ..." */
static void build_items_str(char *out, size_t outsz,
                             CartItem *cart, int cartCount) {
    out[0] = 0;
    size_t pos = 0;
    for (int i = 0; i < cartCount; i++) {
        if (cart[i].itemIndex < 0 || cart[i].itemIndex >= g_count) continue;
        if (i > 0 && pos + 2 < outsz) {
            out[pos++] = ',';
            out[pos++] = ' ';
        }
        int n = snprintf(out + pos, outsz - pos,
                         "%s x%d",
                         g_items[cart[i].itemIndex].sku,
                         cart[i].qty);
        if (n > 0) pos += (size_t)n;
        if (pos >= outsz - 1) break;
    }
    out[pos] = 0;
}

/* ------------------------------------------------------------------ */
/* Funções exportadas                                                   */
/* ------------------------------------------------------------------ */

void audit_init(void) {
    InitializeCriticalSection(&s_lock);

    char dt[32], line[256];
    now_str(dt, sizeof(dt));
    snprintf(line, sizeof(line),
             "%s | -------- | ===== APLICAÇÃO INICIADA =====",
             dt);
    audit_write(line);
}

void audit_item_add(const Item *it) {
    char dt[32], line[512];
    now_str(dt, sizeof(dt));
    snprintf(line, sizeof(line),
             "%s | ADICAO   | SKU=%-12s | Nome=%-32s | Local=%-12s"
             " | Qtd=%d | Preco=%.2f | Custo=%.2f",
             dt,
             it->sku, it->name, it->location,
             it->qty, it->price, it->cost_price);
    audit_write(line);
}

void audit_item_edit(const Item *before, const Item *after) {
    char dt[32], line[1024];
    now_str(dt, sizeof(dt));

    /* Registra apenas os campos que mudaram para manter o log legível */
    char diff[512] = {0};
    size_t pos = 0;

    if (strcmp(before->name, after->name) != 0)
        pos += snprintf(diff+pos, sizeof(diff)-pos,
                        " Nome: [%s]→[%s]", before->name, after->name);
    if (strcmp(before->location, after->location) != 0)
        pos += snprintf(diff+pos, sizeof(diff)-pos,
                        " Local: [%s]→[%s]", before->location, after->location);
    if (before->qty != after->qty)
        pos += snprintf(diff+pos, sizeof(diff)-pos,
                        " Qtd: %d→%d", before->qty, after->qty);
    if (fabsf(before->price - after->price) > 0.001f)
        pos += snprintf(diff+pos, sizeof(diff)-pos,
                        " Preco: %.2f→%.2f", before->price, after->price);
    if (fabsf(before->cost_price - after->cost_price) > 0.001f)
        pos += snprintf(diff+pos, sizeof(diff)-pos,
                        " Custo: %.2f→%.2f", before->cost_price, after->cost_price);

    if (diff[0] == 0)
        snprintf(diff, sizeof(diff), " (sem alterações)");

    snprintf(line, sizeof(line),
             "%s | EDICAO   | SKU=%-12s |%s",
             dt, before->sku, diff);
    audit_write(line);
}

void audit_item_delete(const Item *it) {
    char dt[32], line[512];
    now_str(dt, sizeof(dt));
    snprintf(line, sizeof(line),
             "%s | EXCLUSAO | SKU=%-12s | Nome=%-32s | Local=%-12s"
             " | Qtd=%d | Preco=%.2f | Custo=%.2f",
             dt,
             it->sku, it->name, it->location,
             it->qty, it->price, it->cost_price);
    audit_write(line);
}

void audit_sale(const char *seller,
                CartItem *cart, int cartCount, int discPct) {
    char dt[32];
    now_str(dt, sizeof(dt));

    float subtotal = 0.0f;
    for (int i = 0; i < cartCount; i++)
        if (cart[i].itemIndex >= 0 && cart[i].itemIndex < g_count)
            subtotal += g_items[cart[i].itemIndex].price * (float)cart[i].qty;

    float disc  = subtotal * ((float)discPct / 100.0f);
    float total = subtotal - disc;
    if (total < 0.0f) total = 0.0f;

    char items_str[512];
    build_items_str(items_str, sizeof(items_str), cart, cartCount);

    char line[1024];
    snprintf(line, sizeof(line),
             "%s | VENDA    | Vendedor=%-16s | Desc=%3d%%"
             " | Subtotal=R$%.2f | Total=R$%.2f | Itens=[%s]",
             dt,
             seller ? seller : "(sem nome)",
             discPct, subtotal, total,
             items_str);
    audit_write(line);
}

void audit_quote(const char *seller,
                 CartItem *cart, int cartCount) {
    char dt[32];
    now_str(dt, sizeof(dt));

    float total = 0.0f;
    for (int i = 0; i < cartCount; i++)
        if (cart[i].itemIndex >= 0 && cart[i].itemIndex < g_count)
            total += g_items[cart[i].itemIndex].price * (float)cart[i].qty;

    char items_str[512];
    build_items_str(items_str, sizeof(items_str), cart, cartCount);

    char line[1024];
    snprintf(line, sizeof(line),
             "%s | ORCAM.   | Vendedor=%-16s | Total=R$%.2f | Itens=[%s]",
             dt,
             seller ? seller : "(sem nome)",
             total,
             items_str);
    audit_write(line);
}

/* ------------------------------------------------------------------ */
/* Visualizador de log (DrawAuditLogModal)                              */
/* ------------------------------------------------------------------ */

#define MAX_AUDIT_DISPLAY 500

typedef enum {
    AF_ALL = 0,
    AF_ADICAO,
    AF_EDICAO,
    AF_EXCLUSAO,
    AF_VENDA,
    AF_ORCAM,
    AF_STARTUP,
    AF_COUNT
} AuditFilter;

typedef struct {
    char timestamp[24];
    char op[20];
    char detail[300];
} AuditEntry;

static AuditEntry s_entries[MAX_AUDIT_DISPLAY];
static int        s_entryCount  = 0;
static bool       s_loaded      = false;
static int        s_filter      = AF_ALL;
static int        s_auditScroll = 0;

/* Analisa uma linha do log e preenche uma AuditEntry */
static void parse_audit_line(const char *line, AuditEntry *e) {
    memset(e, 0, sizeof(*e));
    if (!line || !*line) return;

    /* Timestamp: primeiros 19 caracteres */
    if (strlen(line) >= 19) {
        memcpy(e->timestamp, line, 19);
        e->timestamp[19] = 0;
    }

    /* Primeiro " | " → início do campo de operação */
    const char *p1 = strstr(line, " | ");
    if (!p1) { strncpy(e->detail, line, sizeof(e->detail)-1); return; }
    p1 += 3;

    /* Segundo " | " → início dos detalhes */
    const char *p2 = strstr(p1, " | ");
    if (!p2) {
        strncpy(e->op, p1, sizeof(e->op)-1);
        trim_ws_inplace(e->op);
        return;
    }

    int oplen = (int)(p2 - p1);
    if (oplen >= (int)sizeof(e->op)) oplen = (int)sizeof(e->op)-1;
    memcpy(e->op, p1, oplen);
    e->op[oplen] = 0;
    trim_ws_inplace(e->op);

    const char *d = p2 + 3;
    strncpy(e->detail, d, sizeof(e->detail)-1);
    e->detail[sizeof(e->detail)-1] = 0;
    int dlen = (int)strlen(e->detail);
    while (dlen > 0 && (e->detail[dlen-1] == '\n' || e->detail[dlen-1] == '\r'))
        e->detail[--dlen] = 0;
}

/* Carrega as últimas MAX_AUDIT_DISPLAY linhas do arquivo de log */
static void load_audit_entries(void) {
    s_entryCount = 0;
    FILE *f = fopen(AUDIT_FILE, "rb");
    if (!f) { s_loaded = true; return; }

    /* Primeira passagem: conta linhas não-vazias */
    char line[1024];
    int total = 0;
    while (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\r\n")] = 0;
        if (line[0]) total++;
    }

    /* Segunda passagem: pula as primeiras (total - MAX) para exibir só as últimas */
    int skip = (total > MAX_AUDIT_DISPLAY) ? (total - MAX_AUDIT_DISPLAY) : 0;
    int skipped = 0;
    fseek(f, 0, SEEK_SET);

    while (fgets(line, sizeof(line), f) && s_entryCount < MAX_AUDIT_DISPLAY) {
        line[strcspn(line, "\r\n")] = 0;
        if (!line[0]) continue;
        if (skipped < skip) { skipped++; continue; }
        parse_audit_line(line, &s_entries[s_entryCount++]);
    }
    fclose(f);

    /* Começa mostrando os registros mais recentes (fim da lista) */
    s_auditScroll = MAX_AUDIT_DISPLAY;
    s_loaded      = true;
}

/* Retorna true se a entrada passa pelo filtro ativo */
static bool entry_matches(const AuditEntry *e, int filter) {
    if (filter == AF_ALL) return true;
    const char *op = e->op;
    switch (filter) {
        case AF_ADICAO:   return icase_starts_with(op, "ADICAO");
        case AF_EDICAO:   return icase_starts_with(op, "EDICAO");
        case AF_EXCLUSAO: return icase_starts_with(op, "EXCLUSAO");
        case AF_VENDA:    return icase_starts_with(op, "VENDA");
        case AF_ORCAM:    return icase_starts_with(op, "ORCAM");
        case AF_STARTUP:  return (op[0] == '-' || icase_starts_with(op, "STARTUP"));
        default:          return true;
    }
}

/* Cor de destaque por tipo de operação */
static Color op_color(const char *op) {
    if (icase_starts_with(op, "ADICAO"))   return (Color){100, 220, 140, 255};
    if (icase_starts_with(op, "EDICAO"))   return (Color){255, 200,  50, 255};
    if (icase_starts_with(op, "EXCLUSAO")) return (Color){230,  76,  76, 255};
    if (icase_starts_with(op, "VENDA"))    return (Color){ 80, 200, 255, 255};
    if (icase_starts_with(op, "ORCAM"))    return (Color){180, 130, 255, 255};
    return (Color){150, 150, 150, 255}; /* STARTUP / desconhecido */
}

void DrawAuditLogModal(void) {
    if (!s_loaded) load_audit_entries();

    /* --- Layout --- */
    Rectangle panel = {GetScreenWidth()  / 2.0f - 560,
                       GetScreenHeight() / 2.0f - 310,
                       1120, 620};
    DrawRectangleRec(panel, COL_PANEL);
    DrawRectangleLinesEx(panel, 2, (Color){122, 61, 245, 255});

    DrawText("Log de Auditoria", panel.x+16, panel.y+12, 24, COL_TEXT);

    /* Caminho completo do arquivo para o usuário encontrá-lo */
    const char *cwd = GetWorkingDirectory();
    DrawText(TextFormat("Arquivo: %s\\%s", cwd, AUDIT_FILE),
             panel.x + 16, panel.y + 44, 16, COL_TEXT_DIM);

    /* --- Botões de filtro --- */
    const char *labels[AF_COUNT] = {
        "Todos", "Adição", "Edição", "Exclusão", "Venda", "Orçamento", "Sistema"
    };
    Color fColors[AF_COUNT] = {
        COL_TEXT,
        {100, 220, 140, 255},
        {255, 200,  50, 255},
        {230,  76,  76, 255},
        { 80, 200, 255, 255},
        {180, 130, 255, 255},
        {150, 150, 150, 255},
    };

    float bx = panel.x + 16, by = panel.y + 68;  /* era y+50 */
    float bw = 136, bh = 28, bgap = 6;

    for (int i = 0; i < AF_COUNT; i++) {
        Rectangle br = {bx + (float)i * (bw + bgap), by, bw, bh};
        if (GuiButton(br, labels[i])) {
            s_filter = i;
            s_auditScroll = MAX_AUDIT_DISPLAY; /* vai para o fim ao trocar filtro */
        }
        /* Sublinha o filtro ativo */
        if (s_filter == i)
            DrawRectangle((int)br.x, (int)(br.y + bh - 3), (int)bw, 3, fColors[i]);
    }

    /* --- Cabeçalho da lista --- */
    Rectangle hdr = {panel.x+16, panel.y+108, panel.width-32, 26};  /* era y+90 */
    DrawRectangleRec(hdr, COL_HEADER);
    DrawText("Data / Hora",  hdr.x +   8, hdr.y+5, 16, COL_TEXT);
    DrawText("Operação",     hdr.x + 178, hdr.y+5, 16, COL_TEXT);
    DrawText("Detalhes",     hdr.x + 318, hdr.y+5, 16, COL_TEXT);

    /* --- Área de lista --- */
    Rectangle list = {panel.x+16, panel.y+134, panel.width-32, 424};  /* era y+116, h=444 */
    DrawRectangleRec(list, (Color){20, 20, 20, 255});

    int rowH    = 26;
    int visible = (int)(list.height / rowH);

    /* Conta entradas que passam pelo filtro */
    int matchCount = 0;
    for (int i = 0; i < s_entryCount; i++)
        if (entry_matches(&s_entries[i], s_filter)) matchCount++;

    /* Clamp do scroll */
    int maxScroll = (matchCount > visible) ? (matchCount - visible) : 0;
    if (s_auditScroll > maxScroll) s_auditScroll = maxScroll;
    if (s_auditScroll < 0)         s_auditScroll = 0;

    /* Scroll com rodinha */
    if (CheckCollisionPointRec(GetMousePosition(), list)) {
        int wheel = GetMouseWheelMove();
        if (wheel) {
            s_auditScroll -= wheel;
            if (s_auditScroll < 0)         s_auditScroll = 0;
            if (s_auditScroll > maxScroll) s_auditScroll = maxScroll;
        }
    }

    /* --- Scrollbar vertical --- */
    if (matchCount > visible) {
        int sbX = (int)(list.x + list.width - 7);
        int sbH = (int)list.height;
        DrawRectangle(sbX, (int)list.y, 5, sbH, (Color){35, 35, 35, 255});
        int thumbH = (int)((float)visible / matchCount * sbH);
        if (thumbH < 20) thumbH = 20;
        int thumbY = (int)(list.y + (float)s_auditScroll / matchCount * sbH);
        DrawRectangle(sbX, thumbY, 5, thumbH, (Color){122, 61, 245, 200});
    }

    /* --- Linhas da lista --- */
    BeginScissorMode((int)list.x, (int)list.y, (int)list.width - 8, (int)list.height);

    int row = 0, matched = 0;
    for (int i = 0; i < s_entryCount && row < visible; i++) {
        if (!entry_matches(&s_entries[i], s_filter)) continue;
        if (matched++ < s_auditScroll) continue;

        int       y       = (int)(list.y + row * rowH);
        Color     bgColor = (row % 2 == 0) ? COL_ROW_A : COL_ROW_B;
        Rectangle rowRect = {list.x, (float)y, list.width - 8, (float)rowH};
        DrawRectangleRec(rowRect, bgColor);

        Color c = op_color(s_entries[i].op);

        /* Timestamp */
        DrawText(s_entries[i].timestamp,
                 (int)list.x + 8, y + 5, 16, COL_TEXT_DIM);

        /* Badge colorido da operação */
        int opW = MeasureText(s_entries[i].op, 15);
        DrawRectangle((int)list.x + 174, y + 4, opW + 10, rowH - 8,
                      (Color){c.r, c.g, c.b, 30});
        DrawText(s_entries[i].op, (int)list.x + 179, y + 6, 15, c);

        /* Detalhes (clippados) */
        DrawTextClippedCell(
            s_entries[i].detail,
            (Rectangle){list.x + 318, (float)y + 5,
                        list.width - 318 - 16, (float)(rowH - 8)},
            15, COL_TEXT);
        row++;
    }

    if (matchCount == 0) {
        if (s_entryCount == 0) {
            DrawText("Nenhum registro encontrado.", list.x + 16, list.y + 20, 20, COL_TEXT_DIM);
            DrawText("O arquivo será criado automaticamente na primeira operação",
                     list.x + 16, list.y + 50, 18, COL_TEXT_DIM);
            DrawText("(adicionar, editar, excluir item ou finalizar venda/orçamento).",
                     list.x + 16, list.y + 74, 18, COL_TEXT_DIM);
        } else {
            DrawText("Nenhum registro para o filtro selecionado.",
                     list.x + 16, list.y + 20, 20, COL_TEXT_DIM);
        }
    }

    EndScissorMode();

    /* --- Rodapé --- */
    DrawText(TextFormat("Exibindo %d de %d registro(s)  |  F7 abre este painel  |  Arquivo salvo em: %s",
                        matchCount, s_entryCount, GetWorkingDirectory()),
             panel.x + 16, panel.y + panel.height - 42, 15, COL_TEXT_DIM);

    if (GuiButton((Rectangle){panel.x + panel.width - 418,
                               panel.y + panel.height - 52, 110, 36},
                  "⬆ Início")) {
        s_auditScroll = 0;
    }
    if (GuiButton((Rectangle){panel.x + panel.width - 298,
                               panel.y + panel.height - 52, 110, 36},
                  "⬇ Fim")) {
        s_auditScroll = maxScroll;
    }
    if (GuiButton((Rectangle){panel.x + panel.width - 178,
                               panel.y + panel.height - 52, 108, 36},
                  "Recarregar")) {
        s_loaded = false;
    }
    if (GuiButton((Rectangle){panel.x + panel.width - 60,
                               panel.y + panel.height - 52, 44, 36},
                  "X")) {
        g_modal      = MODAL_NONE;
        g_searchEdit = false;
        s_loaded     = false; /* recarrega na próxima abertura */
    }
}
