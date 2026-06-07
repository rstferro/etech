#pragma once
/* =====================================================================
 * db_io.h — Ligação SQLite e migração de dados.
 * ===================================================================== */

#include "app_types.h"

/* Inicializa a base de dados e cria as tabelas se não existirem */
int db_init(void);

/* Fecha a ligação à base de dados de forma segura */
void db_close(void);

/* Lê o CSV antigo e migra tudo para o SQLite (Zero Perda de Dados) */
int db_migrate_from_memory(void);

/* Carrega os produtos da BD SQLite para a memória do programa */
int db_load_estoque(void);