// Servicio C++: calcula la entropía de Shannon y el histograma de bytes
// del archivo recibido. Servidor HTTP mínimo sobre sockets Winsock, usando STL.
//
// Compilar (MinGW):
//   g++ entropy_service.cpp -O2 -std=c++17 -o entropy-cpp.exe -lws2_32
//
// Endpoints:
//   GET  /health  -> {"status":"ok","service":"cpp-entropy"}
//   POST /        -> body = bytes crudos; responde JSON con entropía + bytes distintos.

#include <cstdio>
#include <cstring>
#include <cstdint>
#include <cmath>
#include <string>
#include <vector>
#include <array>
#include <algorithm>

#include <winsock2.h>
#include <ws2tcpip.h>

static const int PORT = 8005;
static const int RECV_CHUNK = 8192;

// Entropía de Shannon en bits por byte (0..8) + número de bytes distintos.
struct EntropyResult {
    double entropy;
    int distinct;
};

static EntropyResult shannon_entropy(const std::vector<unsigned char>& data) {
    std::array<uint64_t, 256> freq{};
    freq.fill(0);
    for (unsigned char b : data) freq[b]++;

    double entropy = 0.0;
    int distinct = 0;
    const double n = static_cast<double>(data.size());
    if (n > 0) {
        for (uint64_t f : freq) {
            if (f == 0) continue;
            distinct++;
            double p = static_cast<double>(f) / n;
            entropy -= p * std::log2(p);
        }
    }
    return {entropy, distinct};
}

// ---- Helpers de socket ----
static void send_all(SOCKET s, const char* data, int len) {
    int sent = 0;
    while (sent < len) {
        int n = send(s, data + sent, len - sent, 0);
        if (n == SOCKET_ERROR) return;
        sent += n;
    }
}

static void send_json(SOCKET s, const std::string& status, const std::string& body) {
    std::string header =
        "HTTP/1.1 " + status + "\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: " + std::to_string(body.size()) + "\r\n"
        "Connection: close\r\n\r\n";
    send_all(s, header.c_str(), static_cast<int>(header.size()));
    send_all(s, body.c_str(), static_cast<int>(body.size()));
}

static long parse_content_length(const std::string& headers) {
    std::string lower = headers;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    size_t pos = lower.find("content-length:");
    if (pos == std::string::npos) return 0;
    return std::strtol(headers.c_str() + pos + 15, nullptr, 10);
}

static void handle_client(SOCKET client) {
    std::vector<unsigned char> buf;
    buf.reserve(RECV_CHUNK);
    char chunk[RECV_CHUNK];

    long header_end = -1;
    // Leer hasta el fin de cabeceras.
    while (header_end < 0) {
        int n = recv(client, chunk, RECV_CHUNK, 0);
        if (n <= 0) { closesocket(client); return; }
        buf.insert(buf.end(), chunk, chunk + n);
        for (size_t i = 0; i + 3 < buf.size(); i++) {
            if (buf[i] == '\r' && buf[i+1] == '\n' && buf[i+2] == '\r' && buf[i+3] == '\n') {
                header_end = static_cast<long>(i + 4);
                break;
            }
        }
        if (buf.size() > 50u * 1024u * 1024u) { closesocket(client); return; }
    }

    std::string headers(reinterpret_cast<char*>(buf.data()), header_end);

    char method[8] = {0}, path[256] = {0};
    std::sscanf(headers.c_str(), "%7s %255s", method, path);

    if (std::strcmp(method, "GET") == 0 && std::strcmp(path, "/health") == 0) {
        send_json(client, "200 OK", "{\"status\":\"ok\",\"service\":\"cpp-entropy\"}");
        closesocket(client);
        return;
    }

    if (std::strcmp(method, "POST") == 0) {
        long content_length = parse_content_length(headers);
        size_t body_have = buf.size() - static_cast<size_t>(header_end);
        while (body_have < static_cast<size_t>(content_length)) {
            int n = recv(client, chunk, RECV_CHUNK, 0);
            if (n <= 0) break;
            buf.insert(buf.end(), chunk, chunk + n);
            body_have += static_cast<size_t>(n);
        }

        std::vector<unsigned char> body(buf.begin() + header_end, buf.end());
        EntropyResult r = shannon_entropy(body);

        char json[200];
        std::snprintf(json, sizeof(json),
            "{\"service\":\"cpp-entropy\",\"entropy\":%.4f,\"distinctBytes\":%d,\"bytes\":%lu}",
            r.entropy, r.distinct, static_cast<unsigned long>(body.size()));
        send_json(client, "200 OK", json);
        closesocket(client);
        return;
    }

    send_json(client, "404 Not Found", "{\"error\":\"not found\"}");
    closesocket(client);
}

int main() {
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        std::fprintf(stderr, "[cpp-entropy] WSAStartup fallo\n");
        return 1;
    }

    SOCKET server = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (server == INVALID_SOCKET) {
        std::fprintf(stderr, "[cpp-entropy] no se pudo crear socket\n");
        WSACleanup();
        return 1;
    }

    int opt = 1;
    setsockopt(server, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<char*>(&opt), sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    addr.sin_port = htons(PORT);

    if (bind(server, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) {
        std::fprintf(stderr, "[cpp-entropy] bind fallo en puerto %d\n", PORT);
        closesocket(server);
        WSACleanup();
        return 1;
    }

    listen(server, 16);
    std::printf("[cpp-entropy] escuchando en http://127.0.0.1:%d  (entropia Shannon)\n", PORT);
    std::fflush(stdout);

    for (;;) {
        SOCKET client = accept(server, nullptr, nullptr);
        if (client == INVALID_SOCKET) continue;
        handle_client(client);
    }

    closesocket(server);
    WSACleanup();
    return 0;
}
