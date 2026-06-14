// BFF (Backend-For-Frontend) en Node.js.
//
// Dos responsabilidades:
//   1. Servir los archivos estáticos del frontend (HTML/CSS/JS).
//   2. Proxear las llamadas /api/* hacia el API Gateway en Go.
//
// Solo módulos integrados de Node (http, fs, path). Sin npm install.
//
// Endpoints expuestos al navegador:
//   GET  /                 -> frontend
//   POST /api/process      -> reenvía al gateway /process
//   GET  /api/history      -> reenvía al gateway /history
//   GET  /api/health       -> reenvía al gateway /health

const http = require("http");
const fs = require("fs");
const path = require("path");

const PORT = 8080;
const GATEWAY_HOST = "127.0.0.1";
const GATEWAY_PORT = 8000;

const FRONTEND_DIR = path.join(__dirname, "..", "..", "frontend");

const MIME = {
  ".html": "text/html; charset=utf-8",
  ".css": "text/css; charset=utf-8",
  ".js": "text/javascript; charset=utf-8",
  ".json": "application/json; charset=utf-8",
  ".ico": "image/x-icon",
};

// --- Reenvío genérico hacia el gateway ---
function proxyToGateway(clientReq, clientRes, targetPath) {
  const chunks = [];
  clientReq.on("data", (c) => chunks.push(c));
  clientReq.on("end", () => {
    const body = Buffer.concat(chunks);

    const options = {
      host: GATEWAY_HOST,
      port: GATEWAY_PORT,
      path: targetPath,
      method: clientReq.method,
      headers: {
        "Content-Type":
          clientReq.headers["content-type"] || "application/octet-stream",
        "Content-Length": body.length,
      },
    };
    if (clientReq.headers["x-filename"]) {
      options.headers["X-Filename"] = clientReq.headers["x-filename"];
    }

    const gwReq = http.request(options, (gwRes) => {
      clientRes.writeHead(gwRes.statusCode, {
        "Content-Type": gwRes.headers["content-type"] || "application/json",
      });
      gwRes.pipe(clientRes);
    });

    gwReq.on("error", (err) => {
      clientRes.writeHead(502, { "Content-Type": "application/json" });
      clientRes.end(
        JSON.stringify({ error: "gateway no disponible", detail: err.message })
      );
    });

    gwReq.end(body);
  });
}

// --- Servir archivos estáticos ---
function serveStatic(req, res) {
  let urlPath = req.url.split("?")[0];
  if (urlPath === "/") urlPath = "/index.html";

  // Evitar path traversal.
  const safePath = path
    .normalize(urlPath)
    .replace(/^(\.\.[\/\\])+/, "");
  const filePath = path.join(FRONTEND_DIR, safePath);

  if (!filePath.startsWith(FRONTEND_DIR)) {
    res.writeHead(403);
    res.end("Forbidden");
    return;
  }

  fs.readFile(filePath, (err, data) => {
    if (err) {
      res.writeHead(404, { "Content-Type": "text/plain; charset=utf-8" });
      res.end("404 - no encontrado: " + urlPath);
      return;
    }
    const ext = path.extname(filePath).toLowerCase();
    res.writeHead(200, { "Content-Type": MIME[ext] || "application/octet-stream" });
    res.end(data);
  });
}

const server = http.createServer((req, res) => {
  const urlPath = req.url.split("?")[0];

  if (urlPath === "/api/process") {
    proxyToGateway(req, res, "/process");
  } else if (urlPath === "/api/history") {
    proxyToGateway(req, res, "/history");
  } else if (urlPath === "/api/health") {
    proxyToGateway(req, res, "/health");
  } else {
    serveStatic(req, res);
  }
});

server.listen(PORT, "127.0.0.1", () => {
  console.log(`[node-bff] sirviendo frontend en http://127.0.0.1:${PORT}`);
  console.log(`[node-bff] proxy /api/* -> http://${GATEWAY_HOST}:${GATEWAY_PORT}`);
});
