/* =====================================================================
 * utils.c — Utilitários de manipulação de strings.
 * ===================================================================== */

#include "platform.h"
#include "utils.h"

void trim(char *s) {
    int i = (int)strlen(s) - 1;
    while (i >= 0 && (s[i] == ' ' || s[i] == '\n' || s[i] == '\r' || s[i] == '\t')) {
        s[i] = 0; i--;
    }
    char *p = s;
    while (*p == ' ' || *p == '\t') p++;
    if (p != s) memmove(s, p, strlen(p) + 1);
}

int starts_with_icase(const char *s, const char *p) {
    while (*p && *s) {
        if (tolower((unsigned char)*s++) != tolower((unsigned char)*p++)) return 0;
    }
    return *p == 0;
}

int contains_icase(const char *h, const char *n) {
    if (!*n) return 1;
    size_t nl = strlen(n);
    for (const char *p = h; *p; p++) {
        size_t i = 0;
        while (i < nl && p[i] && tolower((unsigned char)p[i]) == tolower((unsigned char)n[i]))
            i++;
        if (i == nl) return 1;
    }
    return 0;
}

int icase_starts_with(const char *s, const char *prefix) {
    if (!s || !prefix) return 0;
    while (*s && *prefix) {
        char a = (char)tolower((unsigned char)*s++);
        char b = (char)tolower((unsigned char)*prefix++);
        if (a != b) return 0;
    }
    return *prefix == 0;
}

void trim_ws_inplace(char *s) {
    if (!s) return;
    int L = (int)strlen(s);
    while (L > 0 && (s[L-1] == ' ' || s[L-1] == '\t' || s[L-1] == '\r' || s[L-1] == '\n'))
        s[--L] = 0;
    char *p = s;
    while (*p == ' ' || *p == '\t') p++;
    if (p != s) memmove(s, p, strlen(p) + 1);
}

int split4_any(char *line, const char *delims,
               char **a, char **b, char **c, char **d) {
    if (!line) return 0;
    char *p1 = strpbrk(line, delims); if (!p1) return 0; *p1 = 0; *a = line;
    char *p2 = strpbrk(p1+1, delims); if (!p2) return 0; *p2 = 0; *b = p1+1;
    char *p3 = strpbrk(p2+1, delims); if (!p3) return 0; *p3 = 0; *c = p2+1;
    *d = p3+1;
    return 1;
}
