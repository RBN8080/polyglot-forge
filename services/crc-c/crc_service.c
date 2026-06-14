/*
 * Servicio C: calcula CRC32 (IEEE 802.3) del archivo recibido.
 * Servidor HTTP mínimo escrito directamente sobre sockets Winsock.
 *
 * Compilar (MinGW):
 *   gcc crc_service.c -O2 -o crc-c.exe -lws2_32
 *
 * Endpoints:
 *   GET  /health  -> {"status":"ok","service":"c-crc"}
 *   POST /        -> body = bytes crudos; responde JSON con el CRC32.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include <winsock2.h>
#include <ws2tcpip.h>

#define PORT 8004
#define RECV_CHUNK 8192

/* ---- CRC32 (polinomio reflejado 0xEDB88320) ---- */
static uint32_t crc_table[256];

static void crc32_init(void) {
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t c = i;
        for (int k = 0; k < 8; k++)
            c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
        crc_table[i] = c;
    }
}

static uint32_t crc32_compute(const unsigned char *buf, size_t len) {
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; i++)
        crc = crc_table[(crc ^ buf[i]) & 0xFF] ^ (crc >> 8);
    return crc ^ 0xFFFFFFFFu;
}

/* ---- Helpers de socket ---- */
static void send_all(SOCKET s, const char *data, int len) {
    int sent = 0;
    while (sent < len) {
        int n = send(s, data + sent, len - sent, 0);
        if (n == SOCKET_ERROR) return;
        sent += n;
    }
}

static void send_json(SOCKET s, const char *status, const char *body) {
    char header[256];
    int blen = (int)strlen(body);
    int hlen = snprintf(header, sizeof(header),
        "HTTP/1.1 %s\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: %d\r\n"
        "Connection: close\r\n\r\n",
        status, blen);
    send_all(s, header, hlen);
    send_all(s, body, blen);
}

/* Busca el valor de Content-Length en las cabeceras (insensible a mayúsculas). */
static long parse_content_length(const char *headers) {
    const char *p = headers;
    while (*p) {
        if (_strnicmp(p, "content-length:", 15) == 0) {
            return strtol(p + 15, NULL, 10);
        }
        /* avanzar a la siguiente línea */
        const char *nl = strstr(p, "\r\n");
        if (!nl) break;
        p = nl + 2;
    }
    return 0;
}

static void handle_client(SOCKET client) {
    /* Buffer dinámico para acumular la petición completa. */
    size_t cap = RECV_CHUNK;
    size_t len = 0;
    unsigned char *buf = (unsigned char *)malloc(cap);
    if (!buf) { closesocket(client); return; }

    int header_end = -1;
    /* Leer hasta tener el fin de cabeceras. */
    while (header_end < 0) {
        if (len + RECV_CHUNK > cap) {
            cap *= 2;
            unsigned char *nb = (unsigned char *)realloc(buf, cap);
            if (!nb) { free(buf); closesocket(client); return; }
            buf = nb;
        }
        int n = recv(client, (char *)(buf + len), RECV_CHUNK, 0);
        if (n <= 0) { free(buf); closesocket(client); return; }
        len += (size_t)n;
        /* buscar \r\n\r\n */
        for (size_t i = 0; i + 3 < len; i++) {
            if (buf[i] == '\r' && buf[i+1] == '\n' && buf[i+2] == '\r' && buf[i+3] == '\n') {
                header_end = (int)(i + 4);
                break;
            }
        }
        if (len > 50u * 1024u * 1024u) { free(buf); closesocket(client); return; }
    }

    /* Copiar cabeceras a una cadena terminada en NUL para parsear. */
    char *headers = (char *)malloc(header_end + 1);
    memcpy(headers, buf, header_end);
    headers[header_end] = '\0';

    /* Método y ruta de la primera línea. */
    char method[8] = {0}, path[256] = {0};
    sscanf(headers, "%7s %255s", method, path);

    if (strcmp(method, "GET") == 0 && strcmp(path, "/health") == 0) {
        send_json(client, "200 OK", "{\"status\":\"ok\",\"service\":\"c-crc\"}");
        free(headers); free(buf); closesocket(client);
        return;
    }

    if (strcmp(method, "POST") == 0) {
        long content_length = parse_content_length(headers);
        size_t body_have = len - (size_t)header_end;

        /* Asegurar capacidad para el cuerpo completo. */
        size_t need = (size_t)header_end + (size_t)content_length;
        if (need > cap) {
            unsigned char *nb = (unsigned char *)realloc(buf, need);
            if (!nb) { free(headers); free(buf); closesocket(client); return; }
            buf = nb;
            cap = need;
        }
        while (body_have < (size_t)content_length) {
            int n = recv(client, (char *)(buf + header_end + body_have),
                         (int)(content_length - body_have), 0);
            if (n <= 0) break;
            body_have += (size_t)n;
        }

        uint32_t crc = crc32_compute(buf + header_end, body_have);
        char json[160];
        snprintf(json, sizeof(json),
            "{\"service\":\"c-crc\",\"algo\":\"crc32\",\"crc32\":\"%08x\",\"bytes\":%lu}",
            crc, (unsigned long)body_have);
        send_json(client, "200 OK", json);
        free(headers); free(buf); closesocket(client);
        return;
    }

    send_json(client, "404 Not Found", "{\"error\":\"not found\"}");
    free(headers); free(buf); closesocket(client);
}

int main(void) {
    crc32_init();

    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        fprintf(stderr, "[c-crc] WSAStartup fallo\n");
        return 1;
    }

    SOCKET server = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (server == INVALID_SOCKET) {
        fprintf(stderr, "[c-crc] no se pudo crear socket\n");
        WSACleanup();
        return 1;
    }

    int opt = 1;
    setsockopt(server, SOL_SOCKET, SO_REUSEADDR, (char *)&opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    addr.sin_port = htons(PORT);

    if (bind(server, (struct sockaddr *)&addr, sizeof(addr)) == SOCKET_ERROR) {
        fprintf(stderr, "[c-crc] bind fallo en puerto %d\n", PORT);
        closesocket(server);
        WSACleanup();
        return 1;
    }

    listen(server, 16);
    printf("[c-crc] escuchando en http://127.0.0.1:%d  (CRC32)\n", PORT);
    fflush(stdout);

    for (;;) {
        SOCKET client = accept(server, NULL, NULL);
        if (client == INVALID_SOCKET) continue;
        handle_client(client); /* secuencial: suficiente para el esqueleto */
    }

    closesocket(server);
    WSACleanup();
    return 0;
}
