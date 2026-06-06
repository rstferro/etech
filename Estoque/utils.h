#pragma once
/* =====================================================================
 * utils.h — Utilitários de manipulação de strings.
 * ===================================================================== */

/* Trim de espaços/newlines nas bordas (in-place) */
void trim(char *s);

/* Retorna 1 se `s` começa por `p`, ignorando maiúsculas/minúsculas */
int starts_with_icase(const char *s, const char *p);

/* Retorna 1 se `h` contém `n`, ignorando maiúsculas/minúsculas */
int contains_icase(const char *h, const char *n);

/* Igual a starts_with_icase; mantido por compatibilidade com código de rede */
int icase_starts_with(const char *s, const char *prefix);

/* Trim in-place usando isspace (compatible com strings de rede) */
void trim_ws_inplace(char *s);

/*
 * Divide `line` em 4 campos usando qualquer delimitador de `delims`.
 * Retorna 1 se encontrou os 4 campos, 0 caso contrário.
 * Modifica `line` in-place (insere '\0' nos delimitadores).
 */
int split4_any(char *line, const char *delims,
               char **a, char **b, char **c, char **d);
