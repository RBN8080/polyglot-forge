#!/usr/bin/env python3
"""
Servicio Python: estadísticas del archivo recibido.

Solo biblioteca estándar (http.server). Calcula tamaño, número de líneas,
proporción de bytes imprimibles, byte más frecuente y adivina si es texto
o binario.

Endpoints:
  GET  /health  -> {"status":"ok","service":"python-stats"}
  POST /        -> body = bytes crudos; responde JSON con las estadísticas.
"""

import json
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

PORT = 8002

# Bytes considerados "texto": imprimibles ASCII + tab, salto de línea, retorno.
_TEXT_BYTES = set(range(0x20, 0x7F)) | {0x09, 0x0A, 0x0D}


def analyze(data: bytes) -> dict:
    size = len(data)
    if size == 0:
        return {
            "service": "python-stats",
            "bytes": 0,
            "lines": 0,
            "printableRatio": 0.0,
            "mostCommonByte": None,
            "kind": "empty",
        }

    printable = sum(1 for b in data if b in _TEXT_BYTES)
    ratio = printable / size
    lines = data.count(b"\n") + (0 if data.endswith(b"\n") else 1)

    counts = [0] * 256
    for b in data:
        counts[b] += 1
    most_common = max(range(256), key=lambda i: counts[i])

    kind = "text" if ratio > 0.85 else "binary"

    return {
        "service": "python-stats",
        "bytes": size,
        "lines": lines,
        "printableRatio": round(ratio, 4),
        "mostCommonByte": most_common,
        "kind": kind,
    }


class Handler(BaseHTTPRequestHandler):
    # Silenciar el log por defecto para no ensuciar la consola.
    def log_message(self, fmt, *args):
        pass

    def _send_json(self, code: int, payload: dict):
        body = json.dumps(payload).encode("utf-8")
        self.send_response(code)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        if self.path == "/health":
            self._send_json(200, {"status": "ok", "service": "python-stats"})
        else:
            self._send_json(404, {"error": "not found"})

    def do_POST(self):
        length = int(self.headers.get("Content-Length", 0))
        data = self.rfile.read(length) if length else b""
        self._send_json(200, analyze(data))


def main():
    server = ThreadingHTTPServer(("127.0.0.1", PORT), Handler)
    print(f"[python-stats] escuchando en http://127.0.0.1:{PORT}  (estadisticas)")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        server.shutdown()


if __name__ == "__main__":
    main()
