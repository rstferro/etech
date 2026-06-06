#pragma once
/* =====================================================================
 * ui_helpers.h — Funções de interface: tema raygui, campos de texto
 *                avançados e campo de valor monetário.
 *
 * IMPORTANTE: ui_helpers.c define RAYGUI_IMPLEMENTATION.
 *             Nenhum outro .c deve fazê-lo.
 * ===================================================================== */

#include "platform.h"

/* Aplica o tema escuro roxo ao raygui */
void ApplyTheme(void);

/* Desenha texto clipado dentro de `bounds` (sem overflow) */
void DrawTextClippedCell(const char *text, Rectangle bounds, int fontSize, Color color);

/*
 * Campo de texto com suporte a:
 *   - clique, arraste para selecionar, Ctrl+A
 *   - Backspace / Delete mantidos
 *   - setas, Home, End
 *   - digitação UTF-8 básica
 *   - Ctrl+C / Ctrl+V gerenciados externamente via g_forceCursorEnd
 *
 * Retorna true se o campo acabou de receber o foco por clique.
 */
bool DrawTextInputBoxAdv(Rectangle bounds, char *text, int cap, bool *editMode);

/*
 * Campo de valor monetário em centavos (estilo maquininha).
 * Digitação empurra dígitos da direita; Backspace remove pelo lado direito.
 */
void DrawMoneyInputBox(Rectangle bounds, int *cents, bool editMode);
