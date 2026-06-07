#pragma once
/* =====================================================================
 * audit.h — Log de auditoria de operações do estoque.
 *
 * Registra em AUDIT_FILE cada adição, edição, exclusão, venda e
 * orçamento com timestamp, operador e dados relevantes.
 *
 * Thread-safe: usa seção crítica interna; não depende de g_csvLock.
 * ===================================================================== */

#include "platform.h"
#include "app_types.h"

#define AUDIT_FILE "audit.log"

/* Inicializa a seção crítica e escreve o marcador de startup no log.
 * Deve ser chamada UMA VEZ em main(), antes de qualquer outra função. */
void audit_init(void);

/* Item adicionado manualmente */
void audit_item_add(const Item *it);

/* Item editado: registra o estado anterior e o novo lado a lado */
void audit_item_edit(const Item *before, const Item *after);

/* Item removido do estoque */
void audit_item_delete(const Item *it);

/* Venda finalizada */
void audit_sale(const char *seller,
                CartItem *cart, int cartCount, int discPct);

/* Orçamento emitido */
void audit_quote(const char *seller,
                 CartItem *cart, int cartCount);

/*
 * Desenha o modal de visualização do log de auditoria.
 * Deve ser chamado dentro do bloco BeginDrawing/EndDrawing quando
 * g_modal == MODAL_AUDIT_LOG.
 */
void DrawAuditLogModal(void);
