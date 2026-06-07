/* =====================================================================
 * undo.c — Pilha de desfazer para operações no estoque.
 * ===================================================================== */

#include "platform.h"
#include "app_config.h"
#include "app_types.h"
#include "app_state.h"
#include "undo.h"

/* ------------------------------------------------------------------ */
/* Estado interno                                                       */
/* ------------------------------------------------------------------ */

typedef struct {
    UndoOpType type;
    int        idx;   /* para EDIT: índice em g_items
                         para DELETE: posição original antes da exclusão */
    Item       item;  /* para ADD: item adicionado (buscamos pelo SKU)
                         para EDIT: estado ANTES da edição
                         para DELETE: item excluído */
} UndoEntry;

static UndoEntry s_stack[UNDO_MAX];
static int       s_top = 0;  /* número de entradas válidas (0 = pilha vazia) */

/* ------------------------------------------------------------------ */
/* Helpers internos                                                     */
/* ------------------------------------------------------------------ */

static void push(UndoEntry *e) {
    if (s_top >= UNDO_MAX) {
        /* Pilha cheia: descarta a entrada mais antiga (desloca para esquerda) */
        memmove(s_stack, s_stack + 1, (UNDO_MAX - 1) * sizeof(UndoEntry));
        s_top = UNDO_MAX - 1;
    }
    s_stack[s_top++] = *e;
}

/* ------------------------------------------------------------------ */
/* Funções exportadas — registro                                        */
/* ------------------------------------------------------------------ */

void undo_push_add(const Item *added) {
    UndoEntry e;
    e.type = UNDO_OP_ADD;
    e.idx  = g_count - 1;  /* item foi adicionado no fim */
    e.item = *added;
    push(&e);
}

void undo_push_edit(int idx, const Item *before) {
    UndoEntry e;
    e.type = UNDO_OP_EDIT;
    e.idx  = idx;
    e.item = *before;
    push(&e);
}

void undo_push_delete(int idx, const Item *deleted) {
    UndoEntry e;
    e.type = UNDO_OP_DELETE;
    e.idx  = idx;
    e.item = *deleted;
    push(&e);
}

/* ------------------------------------------------------------------ */
/* Funções exportadas — controle                                        */
/* ------------------------------------------------------------------ */

void undo_apply(void) {
    if (s_top == 0) return;
    UndoEntry *e = &s_stack[--s_top];

    switch (e->type) {

        case UNDO_OP_ADD: {
            /* Desfazer adição: encontrar pelo SKU e excluir */
            EnterCriticalSection(&g_csvLock);
            for (int i = g_count - 1; i >= 0; i--) {
                if (strcmp(g_items[i].sku, e->item.sku) == 0) {
                    for (int j = i; j < g_count - 1; j++)
                        g_items[j] = g_items[j + 1];
                    g_count--;
                    break;
                }
            }
            LeaveCriticalSection(&g_csvLock);
            break;
        }

        case UNDO_OP_EDIT: {
            /* Desfazer edição: restaurar o estado anterior no mesmo índice */
            EnterCriticalSection(&g_csvLock);
            if (e->idx >= 0 && e->idx < g_count)
                g_items[e->idx] = e->item;
            LeaveCriticalSection(&g_csvLock);
            break;
        }

        case UNDO_OP_DELETE: {
            /* Desfazer exclusão: reinserir na posição original */
            EnterCriticalSection(&g_csvLock);
            if (g_count < MAX_ITEMS) {
                int pos = e->idx;
                if (pos < 0)       pos = 0;
                if (pos > g_count) pos = g_count;
                /* Abre espaço deslocando os itens para a direita */
                for (int i = g_count; i > pos; i--)
                    g_items[i] = g_items[i - 1];
                g_items[pos] = e->item;
                g_count++;
            }
            LeaveCriticalSection(&g_csvLock);
            break;
        }

        default:
            break;
    }
}

void undo_clear(void) {
    s_top = 0;
}

bool undo_has_pending(void) {
    return s_top > 0;
}

const char *undo_description(void) {
    static char buf[160];
    if (s_top == 0) {
        snprintf(buf, sizeof(buf), "nada a desfazer");
        return buf;
    }
    const UndoEntry *e = &s_stack[s_top - 1];
    const char *opname;
    switch (e->type) {
        case UNDO_OP_ADD:    opname = "adi\xC3\xA7\xC3\xA3o";   break;  /* adição   */
        case UNDO_OP_EDIT:   opname = "edi\xC3\xA7\xC3\xA3o";   break;  /* edição   */
        case UNDO_OP_DELETE: opname = "exclus\xC3\xA3o";         break;  /* exclusão */
        default:             opname = "opera\xC3\xA7\xC3\xA3o";  break;  /* operação */
    }
    if (s_top > 1)
        snprintf(buf, sizeof(buf),
                 "desfazer %s de '%.14s' (+%d)",
                 opname, e->item.sku, s_top - 1);
    else
        snprintf(buf, sizeof(buf),
                 "desfazer %s de '%.14s - %.24s'",
                 opname, e->item.sku, e->item.name);
    return buf;
}
