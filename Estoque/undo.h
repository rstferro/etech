#pragma once
/* =====================================================================
 * undo.h — Pilha de desfazer para operações no estoque.
 *
 * Suporta até UNDO_MAX níveis. Cada operação de add/edit/delete
 * empurra um registro; Ctrl+Z inverte a última.
 *
 * A pilha é esvaziada automaticamente quando o CSV é recarregado
 * (load_csv / apply_csv_text), pois o estado base teria mudado.
 * ===================================================================== */

#include "platform.h"
#include "app_types.h"

#define UNDO_MAX 10   /* número máximo de níveis de desfazer */

typedef enum {
    UNDO_OP_NONE = 0,
    UNDO_OP_ADD,      /* desfaz adicionando → exclui o item */
    UNDO_OP_EDIT,     /* desfaz edição      → restaura estado anterior */
    UNDO_OP_DELETE    /* desfaz exclusão    → reinserc na posição original */
} UndoOpType;

/* --- Registrar operações (chamado por csv_io.c) --- */
void undo_push_add   (const Item *added);
void undo_push_edit  (int idx, const Item *before);
void undo_push_delete(int idx, const Item *deleted);

/* --- Controle --- */
void        undo_apply      (void); /* Ctrl+Z: desfaz a operação do topo */
void        undo_clear      (void); /* esvazia a pilha                    */
bool        undo_has_pending(void); /* true se há algo a desfazer         */

/*
 * Descrição legível da operação no topo da pilha.
 * Ex: "desfazer exclusão de '00358' (+2)"
 * Retorna ponteiro para buffer estático interno.
 */
const char *undo_description(void);
