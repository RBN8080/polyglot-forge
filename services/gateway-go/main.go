// API Gateway en Go: entrada única del sistema.
//
// Recibe el archivo subido (bytes crudos en el body) y hace "fan-out"
// concurrente con goroutines hacia los servicios de procesamiento. Luego
// registra el job en el servicio Core (Java) y devuelve un informe agregado.
//
// Solo biblioteca estándar (net/http). Ejecutar:  go run .   (o build a .exe)
//
// Endpoints:
//   GET  /health        -> estado del gateway + estado de cada servicio
//   POST /process       -> body = bytes del archivo; devuelve informe agregado
//   GET  /history       -> proxy a Java Core (historial de jobs)

package main

import (
	"bytes"
	"encoding/json"
	"fmt"
	"io"
	"log"
	"net/http"
	"sync"
	"time"
)

const port = 8000

// Servicios de procesamiento a los que se envían los bytes del archivo.
type service struct {
	name string
	url  string
}

var processors = []service{
	{"rust-hash", "http://127.0.0.1:8003/"},
	{"c-crc", "http://127.0.0.1:8004/"},
	{"cpp-entropy", "http://127.0.0.1:8005/"},
	{"python-stats", "http://127.0.0.1:8002/"},
}

const coreURL = "http://127.0.0.1:8001/jobs"

var httpClient = &http.Client{Timeout: 30 * time.Second}

// Resultado de llamar a un servicio de procesamiento.
type procResult struct {
	Name    string          `json:"name"`
	OK      bool            `json:"ok"`
	Data    json.RawMessage `json:"data,omitempty"`
	Error   string          `json:"error,omitempty"`
	TimeMs  int64           `json:"timeMs"`
}

func callProcessor(svc service, body []byte) procResult {
	start := time.Now()
	res := procResult{Name: svc.name}

	req, err := http.NewRequest(http.MethodPost, svc.url, bytes.NewReader(body))
	if err != nil {
		res.Error = err.Error()
		res.TimeMs = time.Since(start).Milliseconds()
		return res
	}
	req.Header.Set("Content-Type", "application/octet-stream")

	resp, err := httpClient.Do(req)
	if err != nil {
		res.Error = err.Error()
		res.TimeMs = time.Since(start).Milliseconds()
		return res
	}
	defer resp.Body.Close()

	data, _ := io.ReadAll(resp.Body)
	res.TimeMs = time.Since(start).Milliseconds()

	if resp.StatusCode != http.StatusOK {
		res.Error = fmt.Sprintf("status %d", resp.StatusCode)
		return res
	}
	res.OK = true
	res.Data = json.RawMessage(data)
	return res
}

func handleProcess(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodPost {
		http.Error(w, `{"error":"use POST"}`, http.StatusMethodNotAllowed)
		return
	}

	filename := r.Header.Get("X-Filename")
	if filename == "" {
		filename = "archivo.bin"
	}

	body, err := io.ReadAll(r.Body)
	if err != nil {
		http.Error(w, `{"error":"no se pudo leer el cuerpo"}`, http.StatusBadRequest)
		return
	}

	// Fan-out concurrente a todos los procesadores.
	var wg sync.WaitGroup
	results := make([]procResult, len(processors))
	for i, svc := range processors {
		wg.Add(1)
		go func(idx int, s service) {
			defer wg.Done()
			results[idx] = callProcessor(s, body)
		}(i, svc)
	}
	wg.Wait()

	// Construir el informe agregado.
	report := map[string]any{
		"filename":  filename,
		"size":      len(body),
		"timestamp": time.Now().Format(time.RFC3339),
		"results":   results,
	}

	// Registrar el job en el servicio Core (Java). No es bloqueante para
	// la respuesta: si falla, seguimos devolviendo el informe.
	if jobInfo := registerJob(report); jobInfo != nil {
		report["job"] = jobInfo
	}

	writeJSON(w, http.StatusOK, report)
}

func registerJob(report map[string]any) json.RawMessage {
	payload, _ := json.Marshal(map[string]any{
		"filename": report["filename"],
		"size":     report["size"],
		"results":  report["results"],
	})
	resp, err := httpClient.Post(coreURL, "application/json", bytes.NewReader(payload))
	if err != nil {
		log.Printf("[gateway] no se pudo registrar job en core: %v", err)
		return nil
	}
	defer resp.Body.Close()
	data, _ := io.ReadAll(resp.Body)
	return json.RawMessage(data)
}

func handleHistory(w http.ResponseWriter, r *http.Request) {
	resp, err := httpClient.Get("http://127.0.0.1:8001/jobs")
	if err != nil {
		writeJSON(w, http.StatusBadGateway, map[string]string{"error": "core no disponible"})
		return
	}
	defer resp.Body.Close()
	data, _ := io.ReadAll(resp.Body)
	w.Header().Set("Content-Type", "application/json")
	w.WriteHeader(resp.StatusCode)
	w.Write(data)
}

func handleHealth(w http.ResponseWriter, r *http.Request) {
	type svcHealth struct {
		Name string `json:"name"`
		Up   bool   `json:"up"`
	}
	checks := []service{
		{"rust-hash", "http://127.0.0.1:8003/health"},
		{"c-crc", "http://127.0.0.1:8004/health"},
		{"cpp-entropy", "http://127.0.0.1:8005/health"},
		{"python-stats", "http://127.0.0.1:8002/health"},
		{"java-core", "http://127.0.0.1:8001/health"},
	}

	var wg sync.WaitGroup
	out := make([]svcHealth, len(checks))
	for i, s := range checks {
		wg.Add(1)
		go func(idx int, svc service) {
			defer wg.Done()
			resp, err := httpClient.Get(svc.url)
			up := err == nil && resp != nil && resp.StatusCode == http.StatusOK
			if resp != nil {
				resp.Body.Close()
			}
			out[idx] = svcHealth{Name: svc.name, Up: up}
		}(i, s)
	}
	wg.Wait()

	writeJSON(w, http.StatusOK, map[string]any{
		"service":  "go-gateway",
		"status":   "ok",
		"upstream": out,
	})
}

func writeJSON(w http.ResponseWriter, code int, v any) {
	w.Header().Set("Content-Type", "application/json")
	w.Header().Set("Access-Control-Allow-Origin", "*")
	w.WriteHeader(code)
	json.NewEncoder(w).Encode(v)
}

func main() {
	mux := http.NewServeMux()
	mux.HandleFunc("/health", handleHealth)
	mux.HandleFunc("/process", handleProcess)
	mux.HandleFunc("/history", handleHistory)

	addr := fmt.Sprintf("127.0.0.1:%d", port)
	log.Printf("[go-gateway] escuchando en http://%s  (API Gateway)", addr)
	log.Fatal(http.ListenAndServe(addr, mux))
}
