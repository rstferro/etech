/* =====================================================================
 * app_windows.c — Ponto de entrada e loop principal da UI.
 *
 * Compilar (Windows, MinGW/clang):
 *   gcc app_state.c utils.c ui_helpers.c csv_io.c printer.c sales.c \
 *       network.c app_windows.c -o estoque.exe -O2 -Wall -I. \
 *       -lraylib -lwinspool -lws2_32 -lopengl32 -lgdi32 -lwinmm
 *
 * Observação: ajuste PRN_NAME (em app_state.c) para o nome da sua
 *             impressora ESC/POS no Windows.
 * ===================================================================== */

#include "platform.h"
#include "app_config.h"
#include "app_types.h"
#include "app_state.h"
#include "utils.h"
#include "ui_helpers.h"
#include "csv_io.h"
#include "printer.h"
#include "sales.h"
#include "network.h"
#include "audit.h"
#include "undo.h"

int main(void) {
    /* Inicializa travas antes de qualquer thread */
    InitializeCriticalSection(&g_csvLock);
    InitializeCriticalSection(&g_sockLock);
    InitializeCriticalSection(&g_sendLock);
    audit_init();

    /* Servidor TCP em background */
    _beginthreadex(NULL, 0, server_thread, NULL, 0, NULL);

    /* Janela */
    SetConfigFlags(FLAG_WINDOW_RESIZABLE);
    InitWindow(1200, 700, "Estoque de Componentes (Windows/Servidor)");
    SetTargetFPS(60);
    ApplyTheme();

    load_csv(CSV_FILE);
    rebuild_filter();

    /* Auto-exporta vendas do mês se estamos nos últimos EXPORT_DIAS_ANTES_FIM dias */
    VerificarEExportarMesAtual();

    /* Mostra o diretório de trabalho na barra de título para que o usuário
     * saiba exatamente onde estoque.csv e audit.log são salvos */
    SetWindowTitle(TextFormat("Estoque — pasta: %s", GetWorkingDirectory()));

    const int rowHeight    = 34;
    const int headerHeight = 36;

    while (!WindowShouldClose()) {

        /* ---- Atualizações de estado ---- */
        UpdatePeerUIStatus();
        UpdatePrinterStatus();

        /* ---- Atalhos de teclado globais ---- */
        if (IsKeyPressed(KEY_F5) && g_modal == MODAL_NONE) {
            load_csv(CSV_FILE); rebuild_filter(); g_needPush = 1;
        }
        if (IsKeyPressed(KEY_F6) && g_modal == MODAL_NONE) {
            save_csv(CSV_FILE);
        }
        if (IsKeyPressed(KEY_F7)) {
            g_modal = MODAL_AUDIT_LOG; g_searchEdit = false;
        }

        /* ---- Ctrl+Z: desfazer última operação no estoque ---- */
        if (g_modal == MODAL_NONE
            && (IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL))
            && IsKeyPressed(KEY_Z)
            && undo_has_pending()) {
            undo_apply();
            rebuild_filter();
            save_csv(CSV_FILE);
            g_needPush = 1;
        }

        /* ---- Ctrl+C / Ctrl+V ---- */
        if (IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL)) {

            if (IsKeyPressed(KEY_C)) {
                int start  = (g_textCursor < g_textAnchor) ? g_textCursor : g_textAnchor;
                int length = (g_textCursor > g_textAnchor)
                             ? (g_textCursor - g_textAnchor)
                             : (g_textAnchor - g_textCursor);
                if (length > 0) {
                    char        clipBuffer[512] = {0};
                    const char *sourceText = NULL;
                    if      (g_searchEdit) sourceText = g_search;
                    else if (g_editSku)   sourceText = g_form.sku;
                    else if (g_editName)  sourceText = g_form.name;
                    else if (g_editLoc)   sourceText = g_form.location;
                    if (sourceText) {
                        strncpy(clipBuffer, sourceText + start, length);
                        SetClipboardText(clipBuffer);
                    }
                } else {
                    if      (g_searchEdit) SetClipboardText(g_search);
                    else if (g_editSku)   SetClipboardText(g_form.sku);
                    else if (g_editName)  SetClipboardText(g_form.name);
                    else if (g_editLoc)   SetClipboardText(g_form.location);
                }
            }

            if (IsKeyPressed(KEY_V)) {
                const char *clipboardText = GetClipboardText();
                if (clipboardText) {
                    if (g_searchEdit) {
                        strncat(g_search, clipboardText,
                                sizeof(g_search) - strlen(g_search) - 1);
                        g_forceCursorEnd = true;
                    } else if (g_editSku) {
                        strncat(g_form.sku, clipboardText,
                                sizeof(g_form.sku) - strlen(g_form.sku) - 1);
                        g_forceCursorEnd = true;
                    } else if (g_editName) {
                        strncat(g_form.name, clipboardText,
                                sizeof(g_form.name) - strlen(g_form.name) - 1);
                        g_forceCursorEnd = true;
                    } else if (g_editLoc) {
                        strncat(g_form.location, clipboardText,
                                sizeof(g_form.location) - strlen(g_form.location) - 1);
                        g_forceCursorEnd = true;
                    } else if (g_textActiveBuf == g_seller) {
                        strncpy(g_seller, clipboardText, sizeof(g_seller) - 1);
                        g_seller[sizeof(g_seller) - 1] = '\0';
                    }
                }
            }
        }

        /* ---- Page Up / Page Down ---- */
        if (g_modal == MODAL_NONE && !g_searchEdit) {
            if (IsKeyPressed(KEY_PAGE_UP))   g_scrollIndex = 0;
            if (IsKeyPressed(KEY_PAGE_DOWN)) g_scrollIndex = g_count;
        }

        /* ================================================================ */
        BeginDrawing();

        /* Reseta scroll ao pesquisar */
        static char g_lastSearch[128] = "\0";
        if (strcmp(g_search, g_lastSearch) != 0) {
            strcpy(g_lastSearch, g_search);
            g_scrollIndex = 0;
        }

        ClearBackground(COL_BG);

        /* ---- Barra superior ---- */
        Rectangle top = (Rectangle){0, 0, (float)GetScreenWidth(), 60};
        DrawRectangleRec(top, (Color){0, 0, 0, 32});

        const char *title  = "Estoque de Componentes (Windows)";
        int  titleFont = 24;
        int  titleW    = MeasureText(title, titleFont);
        int  xStart    = 16, yTop = 12;
        DrawText(title, xStart, yTop + 3, titleFont, COL_TEXT);

        int xButtons = xStart + titleW + 24;
        Rectangle rAdd   = {(float)xButtons,          (float)yTop, 110, 36};
        Rectangle rEdit  = {(float)(xButtons + 118),  (float)yTop, 110, 36};
        Rectangle rDel   = {(float)(xButtons + 236),  (float)yTop, 110, 36};
        Rectangle rCart  = {(float)(xButtons + 354),  (float)yTop, 120, 36};
        Rectangle rQuote = {(float)(xButtons + 482),  (float)yTop, 120, 36};
        Rectangle rSales = {(float)(xButtons + 610),  (float)yTop, 120, 36};

        float     searchX = rSales.x + rSales.width + 24;
        Rectangle rSearch = {searchX, (float)yTop,
                             (float)(GetScreenWidth() - searchX - 16), 36};

        /* Campo de busca */
        if (g_modal == MODAL_NONE) {
            bool toggled = DrawTextInputBoxAdv(rSearch, g_search,
                                               sizeof(g_search), &g_searchEdit);
            (void)toggled;
            if (forceFocusSearch) { g_searchEdit = true; forceFocusSearch = false; }
            if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER)) rebuild_filter();
        } else {
            bool dummy = false;
            DrawTextInputBoxAdv(rSearch, g_search, sizeof(g_search), &dummy);
        }

        /* Botões da barra superior */
        if (g_modal == MODAL_NONE) {
            if (GuiButton(rAdd,   "Adicionar")) {
                memset(&g_form, 0, sizeof(g_form));
                GerarProximoSKU(g_form.sku, sizeof(g_form.sku));
                g_modal = MODAL_ADD; g_searchEdit = false;
                g_editSku = true; g_editName = false; g_editLoc = false;
                g_editPrice = false; g_editCost = false; g_qtyEdit = false;
            }
            if (GuiButton(rEdit,  "Editar")) {
                if (g_selected >= 0 && g_selected < filtered_count) {
                    g_form  = g_items[filtered_indices[g_selected]];
                    g_modal = MODAL_EDIT; g_searchEdit = false;
                    g_editSku = true; g_editName = false; g_editLoc = false;
                    g_editPrice = false; g_editCost = false; g_qtyEdit = false;
                }
            }
            if (GuiButton(rDel,   "Apagar")) {
                if (g_selected >= 0 && g_selected < filtered_count)
                    { g_modal = MODAL_CONFIRM_DELETE; g_searchEdit = false; }
            }
            if (GuiButton(rCart,  "Carrinho"))  {
                g_modal = MODAL_CART;  g_searchEdit = false;
                cartSel = -1; cartScroll = 0; cartStatus[0] = 0;
            }
            if (GuiButton(rQuote, "Orçamento")) {
                g_modal = MODAL_QUOTE; g_searchEdit = false;
                quoteSel = -1; quoteScroll = 0; quoteStatus[0] = 0;
            }
            if (GuiButton(rSales, "Vendas mês")) {
                g_modal = MODAL_MONTH_SALES; g_searchEdit = false; g_salesScroll = 0;
            }
        } else {
            GuiButton(rAdd, "Adicionar"); GuiButton(rEdit, "Editar");
            GuiButton(rDel, "Apagar");    GuiButton(rCart, "Carrinho");
            GuiButton(rQuote, "Orçamento"); GuiButton(rSales, "Vendas mês");
        }

        /* ---- Tabela principal ---- */
        int left   = 16, right = GetScreenWidth() - 16;
        int topY   = 76, bottom = GetScreenHeight() - 62; /* era -16; agora deixa espaço para o rodapé */
        int c1 = 180, c2 = 610, c3 = 760, c4 = 840, c5 = 960;

        Rectangle rHeader = {left, (float)topY, (float)(right - left), (float)headerHeight};
        DrawRectangleRec(rHeader, COL_HEADER);

        /* Cabeçalhos clicáveis para ordenação
         * Ciclo por clique: nenhum → crescente → decrescente → nenhum */
        const char *hdrLabels[] = {"SKU", "Nome", "Local", "Qtd", "Preco", "Custo"};
        int hdrCols[]  = {SORT_COL_SKU, SORT_COL_NAME, SORT_COL_LOCATION,
                          SORT_COL_QTY, SORT_COL_PRICE, SORT_COL_COST};
        int hdrX[]     = {left, c1, c2, c3, c4, c5};
        int hdrXEnd[]  = {c1,   c2, c3, c4, c5, right};

        bool anyHdrHover = false;
        for (int h = 0; h < 6; h++) {
            Rectangle cell = {(float)hdrX[h], (float)topY,
                              (float)(hdrXEnd[h] - hdrX[h]), (float)headerHeight};
            bool hover  = (g_modal == MODAL_NONE
                           && CheckCollisionPointRec(GetMousePosition(), cell));
            bool active = (g_sortCol == hdrCols[h]);
            if (hover) anyHdrHover = true;

            /* Fundo realçado */
            if (active) DrawRectangleRec(cell, (Color){122, 61, 245,  60});
            if (hover)  DrawRectangleRec(cell, (Color){255, 255, 255, 30});

            /* Texto da coluna */
            Color lblColor = active ? (Color){200, 170, 255, 255} : COL_TEXT;
            DrawText(hdrLabels[h], hdrX[h] + 8, topY + 9, 18, lblColor);

            /* Indicador de direção ao lado do rótulo */
            if (active) {
                int lw = MeasureText(hdrLabels[h], 18);
                DrawText(g_sortAsc ? " ^" : " v",
                         hdrX[h] + 8 + lw, topY + 9, 18,
                         (Color){220, 180, 255, 255});
            }

            /* Sublinha de hover para indicar que é clicável */
            if (hover && !active)
                DrawLine(hdrX[h]+4,    topY + headerHeight - 3,
                         hdrXEnd[h]-4, topY + headerHeight - 3,
                         (Color){180, 180, 180, 140});

            /* Divisor vertical entre colunas */
            if (h < 5)
                DrawLine(hdrXEnd[h], topY + 6, hdrXEnd[h], topY + headerHeight - 6,
                         (Color){60, 60, 60, 255});

            /* Clique: alterna crescente → decrescente → sem ordenação */
            if (hover && IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
                if (g_sortCol == hdrCols[h]) {
                    if (g_sortAsc)  g_sortAsc = false;        /* asc → desc */
                    else {          g_sortCol = SORT_COL_NONE; /* desc → nenhum */
                                    g_sortAsc = true; }
                } else {
                    g_sortCol = hdrCols[h];
                    g_sortAsc = true;                          /* nova coluna → asc */
                }
                g_selected    = -1;
                g_scrollIndex = 0;
                apply_sort();
            }
        }
        /* Cursor de ponteiro ao passar sobre cabeçalho clicável */
        SetMouseCursor(anyHdrHover ? MOUSE_CURSOR_POINTING_HAND : MOUSE_CURSOR_DEFAULT);

        Rectangle rList = {left, topY + headerHeight,
                           (float)(right - left),
                           (float)(bottom - (topY + headerHeight))};
        DrawRectangleRec(rList, COL_PANEL);

        int visibleRows = (int)(rList.height / rowHeight);
        if (visibleRows < 0) visibleRows = 0;

        /* Scroll: rodinha + drag */
        static bool  isDragging  = false;
        static float dragStartY  = 0.0f;

        if (visibleRows > 0 && g_modal == MODAL_NONE) {
            Vector2 mp        = GetMousePosition();
            bool    isHovering = CheckCollisionPointRec(mp, rList);

            if (isHovering) {
                int wheel = GetMouseWheelMove();
                if (wheel) g_scrollIndex -= wheel;
            }
            if (isHovering && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                isDragging = true;
                dragStartY = mp.y;
            }
            if (isDragging) {
                if (IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
                    float deltaY        = mp.y - dragStartY;
                    float sensibilidade = 2.0f;
                    if (deltaY >  sensibilidade) { g_scrollIndex -= 2; dragStartY = mp.y; }
                    if (deltaY < -sensibilidade) { g_scrollIndex += 2; dragStartY = mp.y; }
                } else {
                    isDragging = false;
                }
            }
            if (g_scrollIndex < 0) g_scrollIndex = 0;
            int maxScroll = (filtered_count > visibleRows) ? (filtered_count - visibleRows) : 0;
            if (g_scrollIndex > maxScroll) g_scrollIndex = maxScroll;
        }

        /* Linhas da tabela */
        for (int r = 0; r < visibleRows; r++) {
            int idx  = g_scrollIndex + r;
            if (idx >= filtered_count) break;
            int real = filtered_indices[idx];
            int y    = (int)(rList.y + r * rowHeight);
            Rectangle rowRect = {rList.x, (float)y, rList.width, (float)rowHeight};
            bool selected = (g_selected == idx);

            DrawRectangleRec(rowRect, (r % 2 == 0) ? COL_ROW_A : COL_ROW_B);
            if (selected) DrawRectangleRec(rowRect, COL_HILIGHT);
            if (g_modal == MODAL_NONE
                && CheckCollisionPointRec(GetMousePosition(), rowRect)
                && IsMouseButtonPressed(MOUSE_LEFT_BUTTON))
                g_selected = idx;

            DrawText(g_items[real].sku, left+8, y+8, 18, COL_TEXT);
            DrawTextClippedCell(g_items[real].name,
                                (Rectangle){(float)c1+8, (float)y+8, (float)(c2-c1-16), (float)(rowHeight-10)},
                                18, COL_TEXT);
            DrawTextClippedCell(g_items[real].location,
                                (Rectangle){(float)c2+8, (float)y+8, (float)(c3-c2-16), (float)(rowHeight-10)},
                                18, COL_TEXT_DIM);
            DrawText(TextFormat("%d",     g_items[real].qty),        c3+8, y+8, 18, COL_TEXT);
            DrawText(TextFormat("R$ %.2f",g_items[real].price),      c4+8, y+8, 18, COL_TEXT);
            DrawText(TextFormat("R$ %.2f",g_items[real].cost_price), c5+8, y+8, 18, COL_TEXT_DIM);

            Rectangle rPlus = {rList.x + rList.width - 44, (float)y+4, 36, rowHeight-8};
            if (g_modal == MODAL_NONE) {
                if (GuiButton(rPlus, "+")) {
                    addCart_itemIndex = real; addCart_qty = 1; addCart_qtyEdit = false;
                    g_modal = MODAL_ADD_CHOOSE; g_searchEdit = false;
                }
            } else { GuiButton(rPlus, "+"); }
        }

        /* ---- Rodapé (fundo separado da tabela) ---- */
        DrawRectangle(0, GetScreenHeight() - 58,
                      GetScreenWidth(), 58, COL_PANEL);
        DrawLine(0,            GetScreenHeight() - 58,
                 GetScreenWidth(), GetScreenHeight() - 58,
                 (Color){80, 80, 80, 255});

        Rectangle rFooter = {16, (float)(GetScreenHeight()-48),
                             (float)(GetScreenWidth()-32), 32};
        if (g_modal == MODAL_NONE) {
            if (GuiButton((Rectangle){rFooter.x,     rFooter.y, 120, rFooter.height}, "Salvar CSV (F6)"))
                save_csv(CSV_FILE);
            if (GuiButton((Rectangle){rFooter.x+128, rFooter.y, 140, rFooter.height}, "Carregar CSV (F5)")) {
                load_csv(CSV_FILE); rebuild_filter(); g_needPush = 1;
            }
            if (GuiButton((Rectangle){rFooter.x+276, rFooter.y, 110, rFooter.height}, "Log (F7)")) {
                g_modal = MODAL_AUDIT_LOG; g_searchEdit = false;
            }
        } else {
            GuiButton((Rectangle){rFooter.x,     rFooter.y, 120, rFooter.height}, "Salvar CSV (F6)");
            GuiButton((Rectangle){rFooter.x+128, rFooter.y, 140, rFooter.height}, "Carregar CSV (F5)");
            GuiButton((Rectangle){rFooter.x+276, rFooter.y, 110, rFooter.height}, "Log (F7)");
        }

        const char *linuxTxt = g_linuxConnected ? "Linux conectado" : "Linux desconectado";
        const char *prnTxt   = g_prnConnected   ? "Impressora conectada" : "Impressora desconectada";
        DrawText(TextFormat("Itens: %d | Filtrados: %d | Carrinho: %d | Orçamento: %d | "
                            "Peer: %s | %s | %s",
                            g_count, filtered_count, g_cartCount, g_quoteCount,
                            g_connected ? g_peerStatus : "Desconectado",
                            linuxTxt, prnTxt),
                 rFooter.x+400, rFooter.y+6, 20, COL_TEXT_DIM);

        /* Dica de desfazer alinhada à direita */
        if (undo_has_pending()) {
            const char *undoDesc = undo_description();
            int undoW = MeasureText(undoDesc, 17);
            DrawRectangle(GetScreenWidth() - undoW - 28,
                          (int)rFooter.y + 3, undoW + 16, 26,
                          (Color){60, 40, 90, 180});
            DrawText(undoDesc,
                     GetScreenWidth() - undoW - 20,
                     (int)rFooter.y + 7, 17,
                     (Color){200, 170, 255, 255});
        }

        /* ---- Sobreposição escura para modais ---- */
        if (g_modal != MODAL_NONE)
            DrawRectangle(0, 0, GetScreenWidth(), GetScreenHeight(), (Color){0,0,0,48});

        /* ================================================================ */
        /* MODAL: Adicionar / Editar item                                    */
        /* ================================================================ */
        if (g_modal == MODAL_ADD || g_modal == MODAL_EDIT) {
            Rectangle panel = {GetScreenWidth()/2.0f - 380,
                               GetScreenHeight()/2.0f - 220, 760, 400};
            DrawRectangleRec(panel, COL_PANEL);
            DrawRectangleLinesEx(panel, 2, (Color){122,61,245,255});
            DrawText((g_modal == MODAL_ADD ? "Adicionar Item" : "Editar Item"),
                     panel.x+16, panel.y+12, 24, COL_TEXT);

            int y  = (int)panel.y + 60;
            int x1 = (int)panel.x + 16;
            int w1 = 320, h = 36;

            DrawText("SKU",  x1,      y-22, 18, COL_TEXT_DIM);
            if (DrawTextInputBoxAdv((Rectangle){x1,y,w1,h},
                                    g_form.sku, sizeof(g_form.sku), &g_editSku))
                { g_editName = g_editLoc = g_editPrice = g_editCost = g_qtyEdit = false; }

            DrawText("Nome", x1+360, y-22, 18, COL_TEXT_DIM);
            if (DrawTextInputBoxAdv((Rectangle){x1+360,y,w1,h},
                                    g_form.name, sizeof(g_form.name), &g_editName))
                { g_editSku = g_editLoc = g_editPrice = g_editCost = g_qtyEdit = false; }
            y += 64;

            DrawText("Local",      x1,     y-22, 18, COL_TEXT_DIM);
            if (DrawTextInputBoxAdv((Rectangle){x1,y,w1,h},
                                    g_form.location, sizeof(g_form.location), &g_editLoc))
                { g_editSku = g_editName = g_editPrice = g_editCost = g_qtyEdit = false; }

            DrawText("Quantidade", x1+360, y-22, 18, COL_TEXT_DIM);
            if (GuiSpinner((Rectangle){x1+360,y,140,h}, NULL, &g_form.qty, 0, 100000, g_qtyEdit))
                g_qtyEdit = !g_qtyEdit;
            y += 64;

            /* Preço de venda */
            static int  priceCents  = 0;
            static bool priceBufInit = false;
            if (!priceBufInit) { priceCents = (int)(g_form.price * 100.0f + 0.5f); priceBufInit = true; }
            DrawText("Preço venda (R$)", x1, y-22, 18, COL_TEXT_DIM);
            Rectangle priceRect = (Rectangle){x1, y, 160, h};
            if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON)
                && CheckCollisionPointRec(GetMousePosition(), priceRect))
                { g_editPrice = true; g_editSku = g_editName = g_editLoc = g_editCost = g_qtyEdit = false; }
            DrawMoneyInputBox(priceRect, &priceCents, g_editPrice);

            /* Preço de custo */
            static int  costCents  = 0;
            static bool costBufInit = false;
            if (!costBufInit) { costCents = (int)(g_form.cost_price * 100.0f + 0.5f); costBufInit = true; }
            DrawText("Preço custo (R$)", x1+200, y-22, 18, COL_TEXT_DIM);
            Rectangle costRect = (Rectangle){x1+200, y, 160, h};
            if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON)
                && CheckCollisionPointRec(GetMousePosition(), costRect))
                { g_editCost = true; g_editSku = g_editName = g_editLoc = g_editPrice = g_qtyEdit = false; }
            DrawMoneyInputBox(costRect, &costCents, g_editCost);

            /* Tab entre campos */
            if (IsKeyPressed(KEY_TAB)) {
                if      (g_editSku)   { g_editSku  = false; g_editName  = true; }
                else if (g_editName)  { g_editName = false; g_editLoc   = true; }
                else if (g_editLoc)   { g_editLoc  = false; g_editPrice = true; }
                else if (g_editPrice) { g_editPrice = false; g_editCost = true; }
                else if (g_editCost)  { g_editCost  = false; g_editSku  = true; }
                else                  { g_editSku   = true; }
                g_qtyEdit = false;
            }
            y += 80;

            bool ok     = GuiButton((Rectangle){x1,      y, 140, 40}, "OK");
            bool cancel = GuiButton((Rectangle){x1+160,  y, 140, 40}, "Cancelar");

            if (ok) {
                trim(g_form.sku); trim(g_form.name); trim(g_form.location);
                g_form.price      = (float)priceCents / 100.0f;
                g_form.cost_price = (float)costCents  / 100.0f;
                if (g_form.sku[0] && g_form.name[0]) {
                    if (g_modal == MODAL_ADD) add_item(&g_form);
                    else update_item(filtered_indices[g_selected], &g_form);
                    rebuild_filter();
                    g_modal = MODAL_NONE; g_searchEdit = false;
                    priceBufInit = costBufInit = false;
                    save_csv(CSV_FILE); g_needPush = 1;
                }
            }
            if (cancel) {
                g_modal = MODAL_NONE; g_searchEdit = false;
                priceBufInit = costBufInit = false;
            }

        /* ================================================================ */
        } else if (g_modal == MODAL_CONFIRM_DELETE) {
            Rectangle panel = {GetScreenWidth()/2.0f - 250,
                               GetScreenHeight()/2.0f - 100, 500, 180};
            DrawRectangleRec(panel, COL_PANEL);
            DrawRectangleLinesEx(panel, 2, COL_WARN);
            DrawText("Apagar item selecionado?", panel.x+16, panel.y+20, 24, COL_WARN);

            bool yes = GuiButton((Rectangle){panel.x+60,  panel.y+100, 140, 40}, "Sim");
            bool no  = GuiButton((Rectangle){panel.x+300, panel.y+100, 140, 40}, "Não");
            if (yes) {
                if (g_selected >= 0 && g_selected < filtered_count) {
                    delete_item(filtered_indices[g_selected]);
                    rebuild_filter(); save_csv(CSV_FILE); g_needPush = 1;
                }
                g_modal = MODAL_NONE; g_searchEdit = false;
            }
            if (no) { g_modal = MODAL_NONE; g_searchEdit = false; }

        /* ================================================================ */
        } else if (g_modal == MODAL_ADD_CHOOSE) {
            Rectangle panel = {GetScreenWidth()/2.0f - 260,
                               GetScreenHeight()/2.0f - 150, 520, 300};
            DrawRectangleRec(panel, COL_PANEL);
            DrawRectangleLinesEx(panel, 2, (Color){122,61,245,255});
            DrawText("Adicionar item", panel.x+16, panel.y+12, 24, COL_TEXT);

            if (addCart_itemIndex >= 0 && addCart_itemIndex < g_count) {
                Item *it = &g_items[addCart_itemIndex];
                bool  haveStock    = (it->qty > 0);
                int   maxForSpinner = haveStock ? it->qty : 9999;
                if (addCart_qty > maxForSpinner) addCart_qty = maxForSpinner;
                if (addCart_qty < 1)             addCart_qty = 1;

                DrawText(TextFormat("SKU: %s", it->sku),       panel.x+16, panel.y+52, 18, COL_TEXT);
                DrawText(TextFormat("%.48s",   it->name),      panel.x+16, panel.y+74, 18, COL_TEXT_DIM);
                DrawText(TextFormat("Disponível: %d   Preço: R$ %.2f", it->qty, it->price),
                         panel.x+16, panel.y+96, 18, COL_TEXT_DIM);
                DrawText("Qtd:", panel.x+16, panel.y+136, 18, COL_TEXT_DIM);
                if (GuiSpinner((Rectangle){panel.x+60, panel.y+132, 110, 30}, NULL,
                               &addCart_qty, 1, maxForSpinner, addCart_qtyEdit))
                    addCart_qtyEdit = !addCart_qtyEdit;

                bool addToCart  = GuiButton((Rectangle){panel.x+190, panel.y+128, 150, 36},
                                            haveStock ? "Ao Carrinho" : "Sem estoque");
                bool addToQuote = GuiButton((Rectangle){panel.x+350, panel.y+128, 150, 36},
                                            "Ao Orçamento");
                bool close      = GuiButton((Rectangle){panel.x+panel.width-100,
                                                         panel.y+panel.height-48, 84, 30}, "Fechar");
                if (addToCart && haveStock) {
                    int q = addCart_qty;
                    if (q < 1) q = 1; if (q > it->qty) q = it->qty;
                    if (q > 0) cartAdd(addCart_itemIndex, q);
                    g_modal = MODAL_NONE;
                }
                if (addToQuote) {
                    int q = addCart_qty;
                    if (q < 1) q = 1; if (q > 9999) q = 9999;
                    quoteAdd(addCart_itemIndex, q);
                    g_modal = MODAL_NONE;
                }
                if (close) g_modal = MODAL_NONE;
            } else {
                DrawText("Item inválido.", panel.x+16, panel.y+72, 20, COL_WARN);
                if (GuiButton((Rectangle){panel.x+panel.width-96,
                                           panel.y+panel.height-46, 80, 30}, "Fechar"))
                    g_modal = MODAL_NONE;
            }

        /* ================================================================ */
        } else if (g_modal == MODAL_CART) {
            Rectangle panel = {GetScreenWidth()/2.0f - 520,
                               GetScreenHeight()/2.0f - 270, 1040, 540};
            DrawRectangleRec(panel, COL_PANEL);
            DrawRectangleLinesEx(panel, 2, (Color){122,61,245,255});
            DrawText("Carrinho de Vendas", panel.x+16, panel.y+12, 24, COL_TEXT);

            DrawText("Vendedor", panel.x+16, panel.y+52, 18, COL_TEXT_DIM);
            static bool sellerEdit = true;
            DrawTextInputBoxAdv((Rectangle){panel.x+16, panel.y+72, 280, 36},
                                 g_seller, sizeof(g_seller), &sellerEdit);

            DrawText("Desc %", panel.x+320, panel.y+52, 18, COL_TEXT_DIM);
            if (GuiSpinner((Rectangle){panel.x+320, panel.y+72, 120, 36}, NULL,
                           &g_cartDiscPct, 0, 100, g_cartDiscEdit))
                g_cartDiscEdit = !g_cartDiscEdit;

            Rectangle cartHeader = {panel.x+16, panel.y+120, panel.width-32, 30};
            DrawRectangleRec(cartHeader, COL_HEADER);
            int xSku   = (int)cartHeader.x+8,  xName  = (int)cartHeader.x+160;
            int xQtyC  = (int)cartHeader.x+520, xPrice = (int)cartHeader.x+600;
            int xTotal = (int)cartHeader.x+740;
            DrawText("SKU",   xSku,   cartHeader.y+6, 18, COL_TEXT);
            DrawText("Nome",  xName,  cartHeader.y+6, 18, COL_TEXT);
            DrawText("Qtd",   xQtyC,  cartHeader.y+6, 18, COL_TEXT);
            DrawText("Preço", xPrice, cartHeader.y+6, 18, COL_TEXT);
            DrawText("Total", xTotal, cartHeader.y+6, 18, COL_TEXT);

            Rectangle cartListR = {panel.x+16, panel.y+150, panel.width-32, 260};
            DrawRectangleRec(cartListR, COL_PANEL);
            int rows = (int)(cartListR.height / 30); if (rows < 0) rows = 0;

            if (CheckCollisionPointRec(GetMousePosition(), cartListR)) {
                int wheel = GetMouseWheelMove();
                if (wheel) {
                    cartScroll -= wheel;
                    if (cartScroll < 0) cartScroll = 0;
                    int maxS = (g_cartCount > rows) ? (g_cartCount - rows) : 0;
                    if (cartScroll > maxS) cartScroll = maxS;
                }
            }

            float subtotalAll = 0.0f;
            for (int i = 0; i < g_cartCount; i++)
                subtotalAll += g_items[g_cart[i].itemIndex].price * (float)g_cart[i].qty;

            for (int r = 0; r < rows; r++) {
                int idx = cartScroll + r; if (idx >= g_cartCount) break;
                int y   = (int)(cartListR.y + r * 30);
                Rectangle row = {cartListR.x, (float)y, cartListR.width, 30};
                DrawRectangleRec(row, (r%2==0) ? COL_ROW_A : COL_ROW_B);
                if (cartSel == idx) DrawRectangleRec(row, COL_HILIGHT);
                if (CheckCollisionPointRec(GetMousePosition(), row)
                    && IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) cartSel = idx;
                Item *it = &g_items[g_cart[idx].itemIndex];
                float lt = it->price * (float)g_cart[idx].qty;
                DrawText(TextFormat("%-12s",     it->sku),         xSku,   y+7, 18, COL_TEXT);
                DrawText(TextFormat("%-48.48s",  it->name),        xName,  y+7, 18, COL_TEXT);
                DrawText(TextFormat("%d",         g_cart[idx].qty), xQtyC,  y+7, 18, COL_TEXT);
                DrawText(TextFormat("R$ %.2f",    it->price),       xPrice, y+7, 18, COL_TEXT);
                DrawText(TextFormat("R$ %.2f",    lt),              xTotal, y+7, 18, COL_TEXT);
            }

            float disc       = subtotalAll * (g_cartDiscPct / 100.0f);
            float totalGeral = subtotalAll - disc; if (totalGeral < 0) totalGeral = 0;
            DrawText(TextFormat("Subtotal: R$ %.2f   Desconto: %d%% (-R$ %.2f)   Total: R$ %.2f",
                                subtotalAll, g_cartDiscPct, disc, totalGeral),
                     panel.x+16, panel.y+420, 24, COL_TEXT);

            if (GuiButton((Rectangle){panel.x+16,  panel.y+456, 130, 36}, "Remover"))
                { if (cartSel >= 0) cartRemoveAt(cartSel); }
            if (GuiButton((Rectangle){panel.x+156, panel.y+456, 160, 36}, "Limpar carrinho"))
                cartClear();

            bool okCart     = GuiButton((Rectangle){panel.x+panel.width-340, panel.y+456, 160, 36},
                                         "Finalizar venda");
            bool cancelCart = GuiButton((Rectangle){panel.x+panel.width-170, panel.y+456, 140, 36},
                                         "Cancelar");

            if (okCart) {
                trim(g_seller);
                if (!g_seller[0])        snprintf(cartStatus, sizeof(cartStatus), "Informe o nome do vendedor.");
                else if (g_cartCount==0) snprintf(cartStatus, sizeof(cartStatus), "Carrinho vazio.");
                else {
                    int invalid = -1;
                    for (int i = 0; i < g_cartCount; i++) {
                        if (g_cart[i].qty <= 0 || g_cart[i].qty > g_items[g_cart[i].itemIndex].qty)
                            { invalid = i; break; }
                    }
                    if (invalid >= 0) {
                        Item *it = &g_items[g_cart[invalid].itemIndex];
                        snprintf(cartStatus, sizeof(cartStatus),
                                 "Estoque insuficiente para %s (disp. %d)", it->sku, it->qty);
                    } else {
                        EnterCriticalSection(&g_csvLock);
                        for (int i = 0; i < g_cartCount; i++)
                            g_items[g_cart[i].itemIndex].qty -= g_cart[i].qty;
                        LeaveCriticalSection(&g_csvLock);

                        append_sales_csv(g_seller, g_cart, g_cartCount, g_cartDiscPct);
                        save_csv(CSV_FILE);
                        rebuild_filter();

                        int pr1 = PrintCurrentCartReceipt("E-TECH - VIA DA LOJA");
                        WaitTime(0.5);
                        int pr2 = PrintCurrentCartReceipt("E-TECH - VIA DO CLIENTE");
                        if (pr1 != 0 || pr2 != 0)
                            snprintf(cartStatus, sizeof(cartStatus),
                                     "Falha ao imprimir (cod %d/%d).", pr1, pr2);
                        else cartStatus[0] = 0;

                        g_needPush = 1; cartClear();
                        g_modal = MODAL_NONE; g_searchEdit = false;
                    }
                }
            }
            if (cancelCart) { g_modal = MODAL_NONE; g_searchEdit = false; cartStatus[0] = 0; }
            if (cartStatus[0]) DrawText(cartStatus, panel.x+340, panel.y+462, 20, COL_WARN);

        /* ================================================================ */
        } else if (g_modal == MODAL_MONTH_SALES) {
            DrawMonthlySalesModal();

        } else if (g_modal == MODAL_AUDIT_LOG) {
            DrawAuditLogModal();

        /* ================================================================ */
        } else if (g_modal == MODAL_QUOTE) {
            Rectangle panel = {GetScreenWidth()/2.0f - 520,
                               GetScreenHeight()/2.0f - 270, 1040, 540};
            DrawRectangleRec(panel, COL_PANEL);
            DrawRectangleLinesEx(panel, 2, (Color){122,61,245,255});
            DrawText("Orçamento", panel.x+16, panel.y+12, 24, COL_TEXT);

            DrawText("Vendedor", panel.x+16, panel.y+52, 18, COL_TEXT_DIM);
            static bool sellerEdit2 = false;
            DrawTextInputBoxAdv((Rectangle){panel.x+16, panel.y+72, 280, 36},
                                 g_seller, sizeof(g_seller), &sellerEdit2);

            DrawText("Desc %", panel.x+320, panel.y+52, 18, COL_TEXT_DIM);
            if (GuiSpinner((Rectangle){panel.x+320, panel.y+72, 120, 36}, NULL,
                           &g_quoteDiscPct, 0, 100, g_quoteDiscEdit))
                g_quoteDiscEdit = !g_quoteDiscEdit;

            Rectangle cartHeader = {panel.x+16, panel.y+120, panel.width-32, 30};
            DrawRectangleRec(cartHeader, COL_HEADER);
            int xSku   = (int)cartHeader.x+8,  xName  = (int)cartHeader.x+160;
            int xQtyQ  = (int)cartHeader.x+520, xPrice = (int)cartHeader.x+600;
            int xTotal = (int)cartHeader.x+740;
            DrawText("SKU",   xSku,   cartHeader.y+6, 18, COL_TEXT);
            DrawText("Nome",  xName,  cartHeader.y+6, 18, COL_TEXT);
            DrawText("Qtd",   xQtyQ,  cartHeader.y+6, 18, COL_TEXT);
            DrawText("Preço", xPrice, cartHeader.y+6, 18, COL_TEXT);
            DrawText("Total", xTotal, cartHeader.y+6, 18, COL_TEXT);

            Rectangle quoteListR = {panel.x+16, panel.y+150, panel.width-32, 260};
            DrawRectangleRec(quoteListR, COL_PANEL);
            int rows = (int)(quoteListR.height / 30); if (rows < 0) rows = 0;

            if (CheckCollisionPointRec(GetMousePosition(), quoteListR)) {
                int wheel = GetMouseWheelMove();
                if (wheel) {
                    quoteScroll -= wheel;
                    if (quoteScroll < 0) quoteScroll = 0;
                    int maxS = (g_quoteCount > rows) ? (g_quoteCount - rows) : 0;
                    if (quoteScroll > maxS) quoteScroll = maxS;
                }
            }

            float subtotalAll = 0.0f;
            for (int i = 0; i < g_quoteCount; i++)
                subtotalAll += g_items[g_quote[i].itemIndex].price * (float)g_quote[i].qty;

            for (int r = 0; r < rows; r++) {
                int idx = quoteScroll + r; if (idx >= g_quoteCount) break;
                int y   = (int)(quoteListR.y + r * 30);
                Rectangle row = {quoteListR.x, (float)y, quoteListR.width, 30};
                DrawRectangleRec(row, (r%2==0) ? COL_ROW_A : COL_ROW_B);
                if (quoteSel == idx) DrawRectangleRec(row, COL_HILIGHT);
                if (CheckCollisionPointRec(GetMousePosition(), row)
                    && IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) quoteSel = idx;
                Item *it = &g_items[g_quote[idx].itemIndex];
                float lt = it->price * (float)g_quote[idx].qty;
                DrawText(TextFormat("%-12s",    it->sku),          xSku,   y+7, 18, COL_TEXT);
                DrawText(TextFormat("%-48.48s", it->name),         xName,  y+7, 18, COL_TEXT);
                DrawText(TextFormat("%d",        g_quote[idx].qty), xQtyQ,  y+7, 18, COL_TEXT);
                DrawText(TextFormat("R$ %.2f",   it->price),        xPrice, y+7, 18, COL_TEXT);
                DrawText(TextFormat("R$ %.2f",   lt),               xTotal, y+7, 18, COL_TEXT);
            }

            float disc       = subtotalAll * (g_quoteDiscPct / 100.0f);
            float totalGeral = subtotalAll - disc; if (totalGeral < 0) totalGeral = 0;
            DrawText(TextFormat("Subtotal: R$ %.2f   Desconto: %d%% (-R$ %.2f)   Total Orç.: R$ %.2f",
                                subtotalAll, g_quoteDiscPct, disc, totalGeral),
                     panel.x+16, panel.y+420, 24, COL_TEXT);

            if (GuiButton((Rectangle){panel.x+16,  panel.y+456, 130, 36}, "Remover"))
                { if (quoteSel >= 0) quoteRemoveAt(quoteSel); }
            if (GuiButton((Rectangle){panel.x+156, panel.y+456, 160, 36}, "Limpar orçamento"))
                quoteClear();

            /* Transferir orçamento → carrinho */
            bool addCart = GuiButton((Rectangle){panel.x+panel.width-550, panel.y+456, 180, 36},
                                      "Add. ao Carrinho");
            if (addCart) {
                if (g_quoteCount == 0) {
                    snprintf(quoteStatus, sizeof(quoteStatus), "Orçamento vazio.");
                } else {
                    for (int i = 0; i < g_quoteCount; i++)
                        g_cart[g_cartCount++] = g_quote[i];
                    quoteClear();
                    g_modal = MODAL_NONE; g_searchEdit = false; quoteStatus[0] = 0;
                }
            }

            bool okQuote     = GuiButton((Rectangle){panel.x+panel.width-360, panel.y+456, 180, 36},
                                          "Finalizar orçamento");
            bool cancelQuote = GuiButton((Rectangle){panel.x+panel.width-170, panel.y+456, 140, 36},
                                          "Cancelar");

            if (okQuote) {
                trim(g_seller);
                if (!g_seller[0])         snprintf(quoteStatus, sizeof(quoteStatus), "Informe o nome do vendedor.");
                else if (g_quoteCount==0) snprintf(quoteStatus, sizeof(quoteStatus), "Orçamento vazio.");
                else {
                    append_quote_csv(g_seller, g_quote, g_quoteCount);
                    save_csv(CSV_FILE);
                    int pr = PrintCurrentQuoteReceipt("E-TECH");
                    if (pr != 0)
                        snprintf(quoteStatus, sizeof(quoteStatus), "Falha ao imprimir (cod %d).", pr);
                    else quoteStatus[0] = 0;
                    g_needPush = 1; quoteClear();
                    g_modal = MODAL_NONE; g_searchEdit = false;
                }
            }
            if (cancelQuote) { g_modal = MODAL_NONE; g_searchEdit = false; quoteStatus[0] = 0; }
            if (quoteStatus[0]) DrawText(quoteStatus, panel.x+360, panel.y+462, 20, COL_WARN);
        }

        EndDrawing();

        /* ---- Sincronização assíncrona Windows → Linux ---- */
        if (g_needPush      && g_connected)
            if (push_to_client_if_connected()      == 0) g_needPush      = 0;
        if (g_salesNeedPush && g_connected)
            if (push_sales_to_client_if_connected() == 0) g_salesNeedPush = 0;
    }

    save_csv(CSV_FILE);
    DeleteCriticalSection(&g_sendLock);
    DeleteCriticalSection(&g_sockLock);
    DeleteCriticalSection(&g_csvLock);
    CloseWindow();
    return 0;
}
