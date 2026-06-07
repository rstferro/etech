#pragma once
/* =====================================================================
 * csv_io.h — Leitura/escrita de CSV e operações CRUD no estoque.
 * ===================================================================== */

#include "platform.h"
#include "app_types.h"

/* ------- Serialização CSV ------- */
int  save_csv(const char *path);
int  load_csv(const char *path);

/* Serializa o estoque em memória para um buffer de texto (inclui #STAMP) */
void csv_to_text(char **outBuf, int *outLen);

/* Aplica um blob CSV recebido da rede (respeita carimbo de versão) */
void apply_csv_text(const char *txt);

/* ------- Filtro / CRUD ------- */
void rebuild_filter(void);
void apply_sort(void);   /* reordena filtered_indices[] pelo g_sortCol/g_sortAsc atuais */
void add_item(const Item *it);
void update_item(int idx, const Item *it);
void delete_item(int idx);

/*
 * Gera o próximo SKU numérico disponível (maior SKU existente + 1),
 * formatado com 5 dígitos e zeros à esquerda (ex: "00359").
 * Escreve o resultado em `buffer` (tamanho `bufferSize`).
 */
void GerarProximoSKU(char *buffer, int bufferSize);
