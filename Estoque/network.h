#pragma once
/* =====================================================================
 * network.h — Servidor TCP persistente Windows ↔ Linux.
 * ===================================================================== */

#include "platform.h"

/* Thread do servidor — aceita conexões e despacha para client_thread */
unsigned __stdcall server_thread(void *arg);

/* Envia CSV do estoque ao cliente conectado; retorna 0 em sucesso */
int push_to_client_if_connected(void);

/* Envia vendas.csv ao cliente conectado; retorna 0 em sucesso */
int push_sales_to_client_if_connected(void);

/* Atualiza g_linuxConnected com o estado atual da conexão */
void UpdatePeerUIStatus(void);
