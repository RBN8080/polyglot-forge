/*
 * Servicio Java "Core": registro de trabajos (jobs) procesados.
 *
 * Usa com.sun.net.httpserver, incluido en el JDK (sin Maven/Gradle).
 * Mantiene el historial en memoria. En el esqueleto no hay base de datos real;
 * es el punto natural para añadir persistencia (JDBC/SQLite) más adelante.
 *
 * Compilar:  javac CoreService.java
 * Ejecutar:  java CoreService
 *
 * Endpoints:
 *   GET  /health -> {"status":"ok","service":"java-core"}
 *   POST /jobs   -> body = JSON con metadatos del job; lo guarda y devuelve jobId.
 *   GET  /jobs   -> devuelve el historial como array JSON.
 */

import com.sun.net.httpserver.HttpServer;
import com.sun.net.httpserver.HttpExchange;

import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.InetSocketAddress;
import java.nio.charset.StandardCharsets;
import java.time.Instant;
import java.util.List;
import java.util.concurrent.CopyOnWriteArrayList;
import java.util.concurrent.atomic.AtomicLong;

public class CoreService {

    static final int PORT = 8001;

    // Historial en memoria. Cada entrada guarda el JSON recibido tal cual.
    record Job(long id, String receivedAt, String data) {}

    static final List<Job> JOBS = new CopyOnWriteArrayList<>();
    static final AtomicLong COUNTER = new AtomicLong(1);

    public static void main(String[] args) throws IOException {
        HttpServer server = HttpServer.create(new InetSocketAddress("127.0.0.1", PORT), 0);

        server.createContext("/health", ex -> {
            sendJson(ex, 200, "{\"status\":\"ok\",\"service\":\"java-core\"}");
        });

        server.createContext("/jobs", CoreService::handleJobs);

        server.setExecutor(java.util.concurrent.Executors.newFixedThreadPool(8));
        server.start();
        System.out.println("[java-core] escuchando en http://127.0.0.1:" + PORT + "  (registro de jobs)");
    }

    static void handleJobs(HttpExchange ex) throws IOException {
        String method = ex.getRequestMethod();

        if ("POST".equalsIgnoreCase(method)) {
            String body = new String(readAll(ex.getRequestBody()), StandardCharsets.UTF_8);
            long id = COUNTER.getAndIncrement();
            Job job = new Job(id, Instant.now().toString(), body.isBlank() ? "{}" : body);
            JOBS.add(job);
            sendJson(ex, 200,
                "{\"service\":\"java-core\",\"jobId\":" + id +
                ",\"stored\":true,\"totalJobs\":" + JOBS.size() + "}");
            return;
        }

        if ("GET".equalsIgnoreCase(method)) {
            StringBuilder sb = new StringBuilder("[");
            for (int i = 0; i < JOBS.size(); i++) {
                Job j = JOBS.get(i);
                if (i > 0) sb.append(",");
                sb.append("{\"jobId\":").append(j.id())
                  .append(",\"receivedAt\":\"").append(j.receivedAt()).append("\"")
                  .append(",\"data\":").append(j.data())
                  .append("}");
            }
            sb.append("]");
            sendJson(ex, 200, sb.toString());
            return;
        }

        sendJson(ex, 405, "{\"error\":\"method not allowed\"}");
    }

    static byte[] readAll(InputStream in) throws IOException {
        return in.readAllBytes();
    }

    static void sendJson(HttpExchange ex, int code, String body) throws IOException {
        byte[] bytes = body.getBytes(StandardCharsets.UTF_8);
        ex.getResponseHeaders().add("Content-Type", "application/json");
        ex.sendResponseHeaders(code, bytes.length);
        try (OutputStream os = ex.getResponseBody()) {
            os.write(bytes);
        }
    }
}
