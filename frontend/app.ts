// Lógica del frontend, escrita en TypeScript y compilada a app.js con:
//   tsc app.ts --target ES2017 --lib ES2017,DOM
//
// Sube el archivo seleccionado al BFF (/api/process), que lo reenvía al
// gateway en Go, y muestra el informe agregado de todos los servicios.

interface ProcResult {
  name: string;
  ok: boolean;
  data?: any;
  error?: string;
  timeMs: number;
}

interface Report {
  filename: string;
  size: number;
  timestamp: string;
  results: ProcResult[];
  job?: { jobId?: number; totalJobs?: number };
}

const $ = (id: string): HTMLElement => {
  const el = document.getElementById(id);
  if (!el) throw new Error(`falta #${id}`);
  return el;
};

function humanSize(bytes: number): string {
  if (bytes < 1024) return `${bytes} B`;
  if (bytes < 1024 * 1024) return `${(bytes / 1024).toFixed(1)} KB`;
  return `${(bytes / (1024 * 1024)).toFixed(2)} MB`;
}

// Da formato amable a la "data" devuelta por cada servicio.
function renderData(r: ProcResult): string {
  if (!r.ok) return `<span class="err">error: ${r.error ?? "?"}</span>`;
  const d = r.data ?? {};
  switch (r.name) {
    case "rust-hash":
      return `SHA-256: <code>${d.hash}</code>`;
    case "c-crc":
      return `CRC32: <code>${d.crc32}</code>`;
    case "cpp-entropy":
      return `Entropía: <b>${d.entropy}</b> bits/byte · ${d.distinctBytes} bytes distintos`;
    case "python-stats":
      return `Tipo: <b>${d.kind}</b> · ${d.lines} líneas · ${(d.printableRatio * 100).toFixed(1)}% imprimible`;
    default:
      return `<code>${JSON.stringify(d)}</code>`;
  }
}

const LABELS: Record<string, string> = {
  "rust-hash": "🦀 Rust — Hash SHA-256",
  "c-crc": "🔧 C — Checksum CRC32",
  "cpp-entropy": "⚙️ C++ — Entropía Shannon",
  "python-stats": "🐍 Python — Estadísticas",
};

function renderReport(report: Report): void {
  const out = $("results");
  const rows = report.results
    .map((r) => {
      const label = LABELS[r.name] ?? r.name;
      return `
      <div class="card ${r.ok ? "ok" : "fail"}">
        <div class="card-head">
          <span class="svc">${label}</span>
          <span class="time">${r.timeMs} ms</span>
        </div>
        <div class="card-body">${renderData(r)}</div>
      </div>`;
    })
    .join("");

  const jobLine = report.job?.jobId
    ? `<p class="job">📒 Registrado en Java Core como job #${report.job.jobId} (total: ${report.job.totalJobs}).</p>`
    : `<p class="job warn">⚠️ No se pudo registrar en Java Core.</p>`;

  out.innerHTML = `
    <h2>Resultado de <code>${report.filename}</code> · ${humanSize(report.size)}</h2>
    <div class="grid">${rows}</div>
    ${jobLine}`;
}

async function processFile(file: File): Promise<void> {
  const out = $("results");
  out.innerHTML = `<p class="loading">Procesando <b>${file.name}</b> en el pipeline…</p>`;

  try {
    const resp = await fetch("/api/process", {
      method: "POST",
      headers: { "X-Filename": file.name, "Content-Type": "application/octet-stream" },
      body: file,
    });
    if (!resp.ok) throw new Error(`HTTP ${resp.status}`);
    const report: Report = await resp.json();
    renderReport(report);
    void loadHistory();
  } catch (e) {
    out.innerHTML = `<p class="err">No se pudo procesar: ${(e as Error).message}</p>`;
  }
}

async function loadHistory(): Promise<void> {
  const out = $("history");
  try {
    const resp = await fetch("/api/history");
    const jobs: Array<{ jobId: number; receivedAt: string; data: any }> =
      await resp.json();
    if (!jobs.length) {
      out.innerHTML = `<p class="muted">Aún no hay trabajos registrados.</p>`;
      return;
    }
    out.innerHTML = jobs
      .slice()
      .reverse()
      .map(
        (j) =>
          `<li>#${j.jobId} · <code>${j.data?.filename ?? "?"}</code> · ${humanSize(
            j.data?.size ?? 0
          )} · <span class="muted">${new Date(j.receivedAt).toLocaleTimeString()}</span></li>`
      )
      .join("");
  } catch {
    out.innerHTML = `<p class="muted">Historial no disponible.</p>`;
  }
}

async function checkHealth(): Promise<void> {
  const dot = $("health");
  try {
    const resp = await fetch("/api/health");
    const h = await resp.json();
    const up = (h.upstream ?? []).filter((s: any) => s.up).length;
    const total = (h.upstream ?? []).length;
    dot.textContent = `Gateway OK · servicios activos: ${up}/${total}`;
    dot.className = up === total ? "health ok" : "health partial";
  } catch {
    dot.textContent = "Gateway no disponible";
    dot.className = "health fail";
  }
}

function init(): void {
  const input = $("file") as HTMLInputElement;
  const drop = $("drop");

  input.addEventListener("change", () => {
    if (input.files && input.files[0]) void processFile(input.files[0]);
  });

  drop.addEventListener("dragover", (e) => {
    e.preventDefault();
    drop.classList.add("hover");
  });
  drop.addEventListener("dragleave", () => drop.classList.remove("hover"));
  drop.addEventListener("drop", (e) => {
    e.preventDefault();
    drop.classList.remove("hover");
    const f = e.dataTransfer?.files?.[0];
    if (f) void processFile(f);
  });

  void checkHealth();
  void loadHistory();
  setInterval(() => void checkHealth(), 5000);
}

document.addEventListener("DOMContentLoaded", init);
