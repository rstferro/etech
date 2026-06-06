/* =====================================================================
 * ui_helpers.c — Implementações de UI: tema raygui, campos de texto
 *                avançados e campo de valor monetário.
 *
 * Este é o ÚNICO arquivo que define RAYGUI_IMPLEMENTATION.
 * ===================================================================== */

#define RAYGUI_SUPPORT_ICONS 0
#define RAYGUI_IMPLEMENTATION
#include "platform.h"

#include "app_config.h"
#include "app_state.h"
#include "ui_helpers.h"

/* ------------------------------------------------------------------ */
/* Helpers internos (não exportados)                                   */
/* ------------------------------------------------------------------ */

static int clampi_local(int v, int lo, int hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static int utf8_prev_index(const char *s, int idx) {
    if (idx <= 0) return 0;
    idx--;
    while (idx > 0 && (((unsigned char)s[idx] & 0xC0) == 0x80)) idx--;
    return idx;
}

static int utf8_next_index(const char *s, int idx) {
    int len = (int)strlen(s);
    if (idx >= len) return len;
    unsigned char c = (unsigned char)s[idx];
    int step = 1;
    if      ((c & 0x80) == 0x00) step = 1;
    else if ((c & 0xE0) == 0xC0) step = 2;
    else if ((c & 0xF0) == 0xE0) step = 3;
    else if ((c & 0xF8) == 0xF0) step = 4;
    return clampi_local(idx + step, 0, len);
}

static int text_width_prefix(const char *s, int bytes, int fontSize) {
    char tmp[1024];
    if (bytes < 0)  bytes = 0;
    if (bytes > (int)sizeof(tmp) - 1) bytes = (int)sizeof(tmp) - 1;
    memcpy(tmp, s, (size_t)bytes);
    tmp[bytes] = 0;
    return MeasureText(tmp, fontSize);
}

static int text_index_from_mouse_x(const char *s, int mouseX, int textX, int fontSize) {
    int len = (int)strlen(s);
    if (mouseX <= textX) return 0;
    int best = len;
    for (int i = 0; i < len; ) {
        int ni  = utf8_next_index(s, i);
        int mid = textX
                + text_width_prefix(s, ni, fontSize)
                - (text_width_prefix(s, ni, fontSize) - text_width_prefix(s, i, fontSize)) / 2;
        if (mouseX < mid) { best = i; break; }
        i = ni;
    }
    return best;
}

static bool repeat_key_ready_text(int key) {
    double now = GetTime();
    if (IsKeyPressed(key)) {
        g_textRepeatKey  = key;
        g_textRepeatNext = now + 0.38;
        return true;
    }
    if (IsKeyDown(key) && g_textRepeatKey == key && now >= g_textRepeatNext) {
        g_textRepeatNext = now + 0.045;
        return true;
    }
    if (g_textRepeatKey == key && !IsKeyDown(key)) g_textRepeatKey = 0;
    return false;
}

static void text_delete_selection(char *text) {
    int a = g_textCursor < g_textAnchor ? g_textCursor : g_textAnchor;
    int b = g_textCursor > g_textAnchor ? g_textCursor : g_textAnchor;
    if (a != b) {
        int len = (int)strlen(text);
        a = clampi_local(a, 0, len);
        b = clampi_local(b, 0, len);
        memmove(text + a, text + b, strlen(text + b) + 1);
        g_textCursor = g_textAnchor = a;
    }
}

static void text_insert_utf8(char *text, int cap, const char *ins, int insBytes) {
    if (!ins || insBytes <= 0) return;
    text_delete_selection(text);
    int len = (int)strlen(text);
    if (len + insBytes >= cap) return;
    memmove(text + g_textCursor + insBytes, text + g_textCursor,
            (size_t)(len - g_textCursor + 1));
    memcpy(text + g_textCursor, ins, (size_t)insBytes);
    g_textCursor += insBytes;
    g_textAnchor  = g_textCursor;
}

static bool repeat_key_ready_money(int key) {
    double now = GetTime();
    if (IsKeyPressed(key)) {
        g_moneyRepeatKey  = key;
        g_moneyRepeatNext = now + 0.38;
        return true;
    }
    if (IsKeyDown(key) && g_moneyRepeatKey == key && now >= g_moneyRepeatNext) {
        g_moneyRepeatNext = now + 0.045;
        return true;
    }
    if (g_moneyRepeatKey == key && !IsKeyDown(key)) g_moneyRepeatKey = 0;
    return false;
}

static int money_to_cents(float v) {
    if (v < 0.0f) v = 0.0f;
    return (int)(v * 100.0f + 0.5f);
}

static void cents_to_money_text(char *out, size_t outsz, int cents) {
    if (cents < 0) cents = 0;
    snprintf(out, outsz, "%d.%02d", cents / 100, cents % 100);
}

/* ------------------------------------------------------------------ */
/* Funções exportadas                                                   */
/* ------------------------------------------------------------------ */

void ApplyTheme(void) {
    GuiSetStyle(DEFAULT, TEXT_SIZE,             18);
    GuiSetStyle(DEFAULT, TEXT_SPACING,           1);
    GuiSetStyle(DEFAULT, BACKGROUND_COLOR,  0x121212FF);
    GuiSetStyle(DEFAULT, BORDER_COLOR_NORMAL,  0x7A3DF5FF);
    GuiSetStyle(DEFAULT, BASE_COLOR_NORMAL,    0x7A3DF5FF);
    GuiSetStyle(DEFAULT, TEXT_COLOR_NORMAL,    0xFFFFFFFF);
    GuiSetStyle(DEFAULT, BORDER_COLOR_FOCUSED, 0x9C6CFFFF);
    GuiSetStyle(DEFAULT, BASE_COLOR_FOCUSED,   0x9C6CFFFF);
    GuiSetStyle(DEFAULT, TEXT_COLOR_FOCUSED,   0xFFFFFFFF);
    GuiSetStyle(DEFAULT, BORDER_COLOR_PRESSED, 0x5E2DC2FF);
    GuiSetStyle(DEFAULT, BASE_COLOR_PRESSED,   0x5E2DC2FF);
    GuiSetStyle(DEFAULT, TEXT_COLOR_PRESSED,   0xFFFFFFFF);
    GuiSetStyle(DEFAULT, BORDER_COLOR_DISABLED,0x555555FF);
    GuiSetStyle(DEFAULT, BASE_COLOR_DISABLED,  0x444444FF);
    GuiSetStyle(DEFAULT, TEXT_COLOR_DISABLED,  0xAAAAAAFF);
    GuiSetStyle(DEFAULT, LINE_COLOR,           0x3C3C3CFF);
}

void DrawTextClippedCell(const char *text, Rectangle bounds, int fontSize, Color color) {
    if (!text) text = "";
    if (bounds.width <= 0 || bounds.height <= 0) return;
    BeginScissorMode((int)bounds.x, (int)bounds.y,
                     (int)bounds.width, (int)bounds.height);
    DrawText(text, (int)bounds.x, (int)bounds.y, fontSize, color);
    EndScissorMode();
}

bool DrawTextInputBoxAdv(Rectangle bounds, char *text, int cap, bool *editMode) {
    bool clickedFocus = false;
    int  fontSize = GuiGetStyle(DEFAULT, TEXT_SIZE);
    if (fontSize <= 0) fontSize = 18;
    int len = (int)strlen(text);

    if (*editMode && g_textActiveBuf != text) {
        g_textActiveBuf = text;
        g_textCursor    = len;
        g_textAnchor    = len;
        g_textDragging  = false;
    }
    if (g_textActiveBuf == text) {
        g_textCursor = clampi_local(g_textCursor, 0, len);
        g_textAnchor = clampi_local(g_textAnchor, 0, len);
    }

    Vector2 mp   = GetMousePosition();
    bool    hover = CheckCollisionPointRec(mp, bounds);
    int textX = (int)bounds.x + 10;
    int textY = (int)(bounds.y + (bounds.height - fontSize) / 2.0f + 1);

    if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
        if (hover) {
            *editMode = true; clickedFocus = true;
            if (g_textActiveBuf != text) {
                g_textActiveBuf = text;
                g_textCursor    = (int)strlen(text);
                g_textAnchor    = g_textCursor;
            }
            g_textCursor  = text_index_from_mouse_x(text, (int)mp.x, textX, fontSize);
            g_textAnchor  = g_textCursor;
            g_textDragging = true;
        } else if (g_textActiveBuf == text) {
            *editMode      = false;
            g_textDragging = false;
        }
    }
    if (g_textActiveBuf == text && g_textDragging && IsMouseButtonDown(MOUSE_LEFT_BUTTON))
        g_textCursor = text_index_from_mouse_x(text, (int)mp.x, textX, fontSize);
    if (IsMouseButtonReleased(MOUSE_LEFT_BUTTON) && g_textActiveBuf == text)
        g_textDragging = false;

    bool active = (*editMode && g_textActiveBuf == text);
    if (active) {
        bool ctrl  = IsKeyDown(KEY_LEFT_CONTROL)  || IsKeyDown(KEY_RIGHT_CONTROL);
        bool shift = IsKeyDown(KEY_LEFT_SHIFT)    || IsKeyDown(KEY_RIGHT_SHIFT);
        if (ctrl && IsKeyPressed(KEY_A)) { g_textAnchor = 0; g_textCursor = (int)strlen(text); }

        if (repeat_key_ready_text(KEY_BACKSPACE)) {
            if (g_textCursor != g_textAnchor) text_delete_selection(text);
            else if (g_textCursor > 0) {
                int p = utf8_prev_index(text, g_textCursor);
                memmove(text + p, text + g_textCursor, strlen(text + g_textCursor) + 1);
                g_textCursor = g_textAnchor = p;
            }
        }
        if (repeat_key_ready_text(KEY_DELETE)) {
            if (g_textCursor != g_textAnchor) text_delete_selection(text);
            else {
                int n = utf8_next_index(text, g_textCursor);
                if (n != g_textCursor)
                    memmove(text + g_textCursor, text + n, strlen(text + n) + 1);
            }
        }
        if (IsKeyPressed(KEY_LEFT)) {
            if (!shift && g_textCursor != g_textAnchor) {
                g_textCursor = (g_textCursor < g_textAnchor) ? g_textCursor : g_textAnchor;
                g_textAnchor = g_textCursor;
            } else {
                int old = g_textCursor;
                g_textCursor = utf8_prev_index(text, g_textCursor);
                if (!shift) g_textAnchor = g_textCursor;
                else if (g_textAnchor == old) g_textAnchor = old;
            }
        }
        if (IsKeyPressed(KEY_RIGHT)) {
            if (!shift && g_textCursor != g_textAnchor) {
                g_textCursor = (g_textCursor > g_textAnchor) ? g_textCursor : g_textAnchor;
                g_textAnchor = g_textCursor;
            } else {
                int old = g_textCursor;
                g_textCursor = utf8_next_index(text, g_textCursor);
                if (!shift) g_textAnchor = g_textCursor;
                else if (g_textAnchor == old) g_textAnchor = old;
            }
        }
        if (IsKeyPressed(KEY_HOME)) { if (!shift) g_textAnchor = 0; g_textCursor = 0; }
        if (IsKeyPressed(KEY_END))  {
            g_textCursor = (int)strlen(text);
            if (!shift) g_textAnchor = g_textCursor;
        }

        /* Flag de cola: força o cursor para o fim após Ctrl+V */
        if (g_forceCursorEnd) {
            g_textCursor    = (int)strlen(text);
            g_textAnchor    = g_textCursor;
            g_forceCursorEnd = false;
        }

        int ch = GetCharPressed();
        while (ch > 0) {
            if (ch >= 32) {
                int bytes = 0;
                const char *utf8 = CodepointToUTF8(ch, &bytes);
                if (utf8 && bytes > 0) text_insert_utf8(text, cap, utf8, bytes);
            }
            ch = GetCharPressed();
        }
    }

    /* Desenho */
    Color fill   = active ? (Color){42, 34, 62, 255} : (Color){32, 32, 32, 255};
    Color border = active ? (Color){200, 200, 205, 255} : (Color){122, 61, 245, 255};
    DrawRectangleRec(bounds, fill);
    DrawRectangleLinesEx(bounds, active ? 3.0f : 1.0f, border);

    BeginScissorMode((int)bounds.x+4, (int)bounds.y+2,
                     (int)bounds.width-8, (int)bounds.height-4);

    int a = g_textCursor < g_textAnchor ? g_textCursor : g_textAnchor;
    int b = g_textCursor > g_textAnchor ? g_textCursor : g_textAnchor;

    if (active && a != b) {
        int sx = textX + text_width_prefix(text, a, fontSize);
        int ex = textX + text_width_prefix(text, b, fontSize);
        DrawRectangle(sx, (int)bounds.y+6, ex - sx, (int)bounds.height-12,
                      (Color){150, 150, 155, 180});
    }
    DrawText(text, textX, textY, fontSize, COL_TEXT);
    if (active && a != b) {
        char sel[1024];
        int  n = b - a;
        if (n > (int)sizeof(sel)-1) n = (int)sizeof(sel)-1;
        memcpy(sel, text + a, (size_t)n); sel[n] = 0;
        int sx = textX + text_width_prefix(text, a, fontSize);
        DrawText(sel, sx, textY, fontSize, (Color){20, 20, 20, 255});
    }
    if (active) {
        int cx = textX + text_width_prefix(text, g_textCursor, fontSize) + 1;
        DrawRectangle(cx,   (int)bounds.y+6,  3, (int)bounds.height-12,
                      (Color){185, 185, 190, 255});
        DrawRectangle(cx+4, (int)bounds.y+8,  1, (int)bounds.height-16,
                      (Color){235, 235, 235, 255});
    }
    EndScissorMode();
    return clickedFocus;
}

void DrawMoneyInputBox(Rectangle bounds, int *cents, bool editMode) {
    Vector2 mp    = GetMousePosition();
    bool    hover = CheckCollisionPointRec(mp, bounds);

    if (editMode && g_moneyActivePtr != cents) {
        g_moneyActivePtr   = cents;
        g_moneySelectedAll = false;
    }
    if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON) && hover) {
        g_moneyActivePtr   = cents;
        g_moneySelectedAll = false;
        g_moneyDragging    = true;
        g_moneyDragStartX  = mp.x;
    }
    if (g_moneyActivePtr == cents && g_moneyDragging && IsMouseButtonDown(MOUSE_LEFT_BUTTON)) {
        if (mp.x > g_moneyDragStartX + 5.0f || mp.x < g_moneyDragStartX - 5.0f || !hover)
            g_moneySelectedAll = true;
    }
    if (IsMouseButtonReleased(MOUSE_LEFT_BUTTON) && g_moneyActivePtr == cents)
        g_moneyDragging = false;

    bool active = editMode && (g_moneyActivePtr == cents);
    if (active) {
        bool ctrl = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);
        if (ctrl && IsKeyPressed(KEY_A)) g_moneySelectedAll = true;

        int ch = GetCharPressed();
        while (ch > 0) {
            if (ch >= '0' && ch <= '9') {
                if (g_moneySelectedAll) { *cents = 0; g_moneySelectedAll = false; }
                if (*cents < 999999999) *cents = (*cents) * 10 + (ch - '0');
            }
            ch = GetCharPressed();
        }
        if (repeat_key_ready_money(KEY_BACKSPACE)) {
            if (g_moneySelectedAll) { *cents = 0; g_moneySelectedAll = false; }
            else *cents = (*cents) / 10;
        }
        if (repeat_key_ready_money(KEY_DELETE)) {
            *cents = 0;
            g_moneySelectedAll = false;
        }
        if (*cents < 0) *cents = 0;
    }

    char txt[64];
    cents_to_money_text(txt, sizeof(txt), *cents);

    Color fill   = active ? (Color){42, 34, 62, 255}      : (Color){32, 32, 32, 255};
    Color border = active ? (Color){200, 200, 205, 255}   : (Color){122, 61, 245, 255};
    DrawRectangleRec(bounds, fill);
    DrawRectangleLinesEx(bounds, active ? 3.0f : 1.0f, border);

    int fontSize = GuiGetStyle(DEFAULT, TEXT_SIZE);
    if (fontSize <= 0) fontSize = 18;
    int textX = (int)bounds.x + 10;
    int textY = (int)(bounds.y + (bounds.height - fontSize) / 2.0f + 1);
    int textW = MeasureText(txt, fontSize);

    if (active && g_moneySelectedAll) {
        DrawRectangle(textX-2, (int)bounds.y+6, textW+6, (int)bounds.height-12,
                      (Color){150, 150, 155, 180});
        DrawText(txt, textX, textY, fontSize, (Color){20, 20, 20, 255});
    } else {
        DrawText(txt, textX, textY, fontSize, COL_TEXT);
    }

    if (active && !g_moneySelectedAll) {
        int cx = textX + textW + 5;
        if (cx > (int)(bounds.x + bounds.width - 8)) cx = (int)(bounds.x + bounds.width - 8);
        DrawRectangle(cx,   (int)bounds.y+6,  3, (int)bounds.height-12,
                      (Color){185, 185, 190, 255});
        DrawRectangle(cx+4, (int)bounds.y+8,  1, (int)bounds.height-16,
                      (Color){235, 235, 235, 255});
    }
}
