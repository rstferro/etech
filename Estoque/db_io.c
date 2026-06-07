/* =====================================================================
 * db_io.c — Implementação do SQLite (Criação e Migração).
 * ===================================================================== */

#include <stdio.h>
#include <string.h>
#include "platform.h"
#include "app_state.h"
#include "app_types.h"
#include "db_io.h"
#include "sqlite3.h" /* O nosso novo motor! */

static sqlite3 *db = NULL;

int db_init(void) {
    /* Abre ou cria o ficheiro da base de dados */
    int rc = sqlite3_open("etech_dados.db", &db);
    if (rc) {
        printf("Erro ao abrir BD: %s\\n", sqlite3_errmsg(db));
        return -1;
    }

    /* Cria a Tabela de Estoque blindada */
    const char *sql_estoque = 
        "CREATE TABLE IF NOT EXISTS estoque ("
        "sku TEXT PRIMARY KEY, "
        "name TEXT NOT NULL, "
        "location TEXT, "
        "qty INTEGER NOT NULL DEFAULT 0, "
        "price REAL NOT NULL DEFAULT 0.0, "
        "cost_price REAL NOT NULL DEFAULT 0.0);";

    char *errMsg = NULL;
    rc = sqlite3_exec(db, sql_estoque, 0, 0, &errMsg);
    if (rc != SQLITE_OK) {
        printf("Erro SQL: %s\\n", errMsg);
        sqlite3_free(errMsg);
        return -1;
    }

    return 0;
}

void db_close(void) {
    if (db) {
        sqlite3_close(db);
        db = NULL;
    }
}

/* Esta função pega nos teus dados que vieram do CSV e injeta-os no SQLite */
int db_migrate_from_memory(void) {
    if (!db || g_count == 0) return 0;

    sqlite3_exec(db, "BEGIN TRANSACTION;", NULL, NULL, NULL);

    sqlite3_stmt *stmt;
    const char *sql = "INSERT OR IGNORE INTO estoque (sku, name, location, qty, price, cost_price) VALUES (?, ?, ?, ?, ?, ?);";
    
    sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);

    for (int i = 0; i < g_count; i++) {
        sqlite3_bind_text(stmt, 1, g_items[i].sku, -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, g_items[i].name, -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 3, g_items[i].location, -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 4, g_items[i].qty);
        sqlite3_bind_double(stmt, 5, (double)g_items[i].price);
        sqlite3_bind_double(stmt, 6, (double)g_items[i].cost_price);

        sqlite3_step(stmt);
        sqlite3_reset(stmt);
    }

    sqlite3_finalize(stmt);
    sqlite3_exec(db, "COMMIT;", NULL, NULL, NULL);
    
    printf("Migração concluída com sucesso! %d itens guardados no SQLite.\\n", g_count);
    return 1;
}