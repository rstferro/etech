/* =====================================================================
 * network.c — Servidor TCP Windows ↔ Linux: aceita conexão persistente,
 *             responde PULL/PUSH, reencaminha impressão e sincroniza CSV.
 * ===================================================================== */

#include "platform.h"
#include "app_config.h"
#include "app_types.h"
#include "app_state.h"
#include "csv_io.h"
#include "sales.h"
#include "printer.h"
#include "network.h"

/* ------------------------------------------------------------------ */
/* Helpers de protocolo (internos)                                      */
/* ------------------------------------------------------------------ */

static int send_all(SOCKET s, const char *buf, int len) {
    int sent = 0;
    while (sent < len) {
        int n = send(s, buf + sent, len - sent, 0);
        if (n <= 0) return -1;
        sent += n;
    }
    return 0;
}

/*
 * Envia um comando + payload no formato:
 *   <CMD>\n
 *   <payload>
 *   \n.\n
 *
 * Usa g_sendLock para evitar que dois envios simultâneos (thread de cliente
 * e loop principal) se misturem no mesmo socket.
 */
static int send_cmd_payload(SOCKET s, const char *cmd, const char *payload) {
    EnterCriticalSection(&g_sendLock);
    int  rc = 0;
    char head[256];
    int  n = snprintf(head, sizeof(head), "%s\n", cmd);
    if (n <= 0)                                             rc = -1;
    else if (send_all(s, head, n) < 0)                     rc = -1;
    else if (payload && *payload &&
             send_all(s, payload, (int)strlen(payload)) < 0) rc = -1;
    else if (send_all(s, "\n.\n", 3) < 0)                   rc = -1;
    LeaveCriticalSection(&g_sendLock);
    return rc;
}

static int recv_line(SOCKET s, char *buf, int max, int timeout_ms) {
    int i = 0;
    for (;;) {
        fd_set rfds; FD_ZERO(&rfds); FD_SET(s, &rfds);
        struct timeval tv;
        tv.tv_sec  = timeout_ms / 1000;
        tv.tv_usec = (timeout_ms % 1000) * 1000;
        int rv = select(0, &rfds, NULL, NULL, &tv);
        if (rv <= 0) return -1;
        char c; int n = recv(s, &c, 1, 0);
        if (n <= 0) return -1;
        if (c == '\n') { buf[i] = 0; return i; }
        if (i < max - 1) buf[i++] = c;
        else { buf[i] = 0; return i; }
    }
}

static int recv_payload_until_dot(SOCKET s, char *buf, int max, int timeout_ms) {
    int total = 0;
    for (;;) {
        char line[NET_MAXLINE];
        int n = recv_line(s, line, sizeof(line), timeout_ms);
        if (n < 0) return -1;
        if (strcmp(line, ".") == 0) { if (total < max) buf[total] = 0; return total; }
        int need = n + 1;
        if (total + need >= max) return -1;
        memcpy(buf + total, line, n);
        buf[total + n] = '\n';
        total += need;
    }
}

/* ------------------------------------------------------------------ */
/* Thread por conexão (cliente Linux)                                   */
/* ------------------------------------------------------------------ */

static unsigned __stdcall client_thread(void *arg) {
    SOCKET s = (SOCKET)(uintptr_t)arg;
    BOOL on = TRUE;
    setsockopt(s, IPPROTO_TCP, TCP_NODELAY, (const char *)&on, sizeof(on));
    setsockopt(s, SOL_SOCKET,  SO_KEEPALIVE,(const char *)&on, sizeof(on));

    EnterCriticalSection(&g_sockLock);
    g_cliSock    = s;
    g_connected  = 1;
    g_failStreak = 0;
    snprintf(g_peerStatus, sizeof(g_peerStatus), "Conectado: %s", LINUX_IP);
    LeaveCriticalSection(&g_sockLock);

    char line[NET_MAXLINE];
    for (;;) {
        int n = recv_line(s, line, sizeof(line), 8000);
        if (n < 0) { g_failStreak++; if (g_failStreak >= 2) break; else continue; }
        g_failStreak = 0;

        if (strcmp(line, CMD_HELLO) == 0) {
            recv_payload_until_dot(s, line, sizeof(line), 8000);
            send_cmd_payload(s, CMD_ACK, "OK\n");

        } else if (strcmp(line, CMD_PING) == 0) {
            recv_payload_until_dot(s, line, sizeof(line), 8000);
            send_cmd_payload(s, CMD_PONG, "OK\n");

        } else if (strcmp(line, CMD_PULL_CSV) == 0) {
            recv_payload_until_dot(s, line, sizeof(line), 8000);
            char *csv = NULL; int len = 0;
            csv_to_text(&csv, &len);
            send_cmd_payload(s, CMD_ACK, csv);
            free(csv);
            g_needPush = 0; /* entrega concluída */

        } else if (strcmp(line, CMD_PUSH_CSV) == 0) {
            char payload[1<<20];
            int r = recv_payload_until_dot(s, payload, sizeof(payload), 8000);
            if (r >= 0) {
                g_needPush = 0;
                apply_csv_text(payload);
                rebuild_filter();
                send_cmd_payload(s, CMD_ACK, "SYNCED\n");
            } else {
                send_cmd_payload(s, CMD_ERR, "PAYLOAD\n");
            }

        } else if (strcmp(line, CMD_PULL_SALES) == 0) {
            recv_payload_until_dot(s, line, sizeof(line), 8000);
            char *sales = NULL; int len = 0;
            sales_file_to_text(&sales, &len);
            send_cmd_payload(s, CMD_ACK, sales ? sales : "");
            free(sales);
            g_salesNeedPush = 0;

        } else if (strcmp(line, CMD_PUSH_SALES) == 0) {
            char payload[1<<20];
            int r = recv_payload_until_dot(s, payload, sizeof(payload), 8000);
            if (r >= 0) {
                merge_sales_text(payload);
                send_cmd_payload(s, CMD_ACK, "SALES_SYNCED\n");
            } else {
                send_cmd_payload(s, CMD_ERR, "PAYLOAD\n");
            }

        } else if (strcmp(line, CMD_PRINT_SALE) == 0) {
            char payload[1<<20];
            int r = recv_payload_until_dot(s, payload, sizeof(payload), 8000);
            if (r >= 0) {
                int rc = PrintPayloadReceiptDirect("E-TECH", payload, 0);
                if (rc == 0) send_cmd_payload(s, CMD_ACK, "PRINTED\n");
                else         send_cmd_payload(s, CMD_ERR, "PRN\n");
            } else {
                send_cmd_payload(s, CMD_ERR, "PAYLOAD\n");
            }

        } else if (strcmp(line, CMD_PRINT_QUOTE) == 0) {
            char payload[1<<20];
            int r = recv_payload_until_dot(s, payload, sizeof(payload), 8000);
            if (r >= 0) {
                int rc = PrintPayloadReceiptDirect("E-TECH", payload, 1);
                if (rc == 0) send_cmd_payload(s, CMD_ACK, "PRINTED\n");
                else         send_cmd_payload(s, CMD_ERR, "PRN\n");
            } else {
                send_cmd_payload(s, CMD_ERR, "PAYLOAD\n");
            }

        } else if (strcmp(line, CMD_BYE) == 0) {
            recv_payload_until_dot(s, line, sizeof(line), 8000);
            send_cmd_payload(s, CMD_ACK, "BYE\n");
            break;

        } else {
            recv_payload_until_dot(s, line, sizeof(line), 8000);
            send_cmd_payload(s, CMD_ERR, "UNKNOWN\n");
        }
    }

    EnterCriticalSection(&g_sockLock);
    closesocket(s);
    g_cliSock   = INVALID_SOCKET;
    g_connected = 0;
    snprintf(g_peerStatus, sizeof(g_peerStatus), "Desconectado");
    LeaveCriticalSection(&g_sockLock);
    return 0;
}

/* ------------------------------------------------------------------ */
/* Funções exportadas                                                   */
/* ------------------------------------------------------------------ */

unsigned __stdcall server_thread(void *arg) {
    (void)arg;
    WSADATA w;
    WSAStartup(MAKEWORD(2, 2), &w);

    SOCKET ls = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    BOOL on = TRUE;
    setsockopt(ls, SOL_SOCKET, SO_REUSEADDR, (const char *)&on, sizeof(on));

    struct sockaddr_in a;
    memset(&a, 0, sizeof(a));
    a.sin_family      = AF_INET;
    a.sin_addr.s_addr = INADDR_ANY;
    a.sin_port        = htons(NET_TCP_PORT);
    bind(ls, (struct sockaddr *)&a, sizeof(a));
    listen(ls, 2);

    for (;;) {
        SOCKET s = accept(ls, NULL, NULL);
        if (s == INVALID_SOCKET) { Sleep(50); continue; }
        _beginthreadex(NULL, 0, client_thread, (void *)(uintptr_t)s, 0, NULL);
    }
    return 0;
}

int push_to_client_if_connected(void) {
    if (!g_connected) return -1;
    EnterCriticalSection(&g_sockLock);
    SOCKET s = g_cliSock;
    LeaveCriticalSection(&g_sockLock);
    if (s == INVALID_SOCKET) return -1;

    char *csv = NULL; int len = 0;
    csv_to_text(&csv, &len);
    int rc = send_cmd_payload(s, CMD_PUSH_CSV, csv);
    free(csv);

    if (rc < 0) {
        EnterCriticalSection(&g_sockLock);
        if (g_cliSock == s) {
            closesocket(g_cliSock);
            g_cliSock   = INVALID_SOCKET;
            g_connected = 0;
            snprintf(g_peerStatus, sizeof(g_peerStatus), "Desconectado");
        }
        LeaveCriticalSection(&g_sockLock);
    }
    return rc;
}

int push_sales_to_client_if_connected(void) {
    if (!g_connected) return -1;
    EnterCriticalSection(&g_sockLock);
    SOCKET s = g_cliSock;
    LeaveCriticalSection(&g_sockLock);
    if (s == INVALID_SOCKET) return -1;

    char *sales = NULL; int len = 0;
    sales_file_to_text(&sales, &len);
    int rc = send_cmd_payload(s, CMD_PUSH_SALES, sales ? sales : "");
    free(sales);

    if (rc < 0) {
        EnterCriticalSection(&g_sockLock);
        if (g_cliSock == s) {
            closesocket(g_cliSock);
            g_cliSock   = INVALID_SOCKET;
            g_connected = 0;
            snprintf(g_peerStatus, sizeof(g_peerStatus), "Desconectado");
        }
        LeaveCriticalSection(&g_sockLock);
    }
    return rc;
}

void UpdatePeerUIStatus(void) {
    g_linuxConnected = (g_connected != 0);
}
