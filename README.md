# 🔨 polyglot-forge

> Un esqueleto de **microservicios políglotas** listo para forjar tus próximas apps.
> Ocho lenguajes hablando por red (HTTP/JSON), cada uno en lo que mejor hace.

`polyglot-forge` es una **plantilla / campo de entrenamiento**: una arquitectura de
microservicios completa y funcionando de punta a punta, pensada para **aprender**
(redes, concurrencia, cada lenguaje) y para **arrancar proyectos reales** sin
montar todo desde cero.

La demo incluida es un **procesador de archivos distribuido**: subes un archivo y
cinco servicios lo analizan en paralelo. Pero el valor está en el **esqueleto**:
cambia lo que hace cada servicio y tienes una app nueva.

Todo usa **solo bibliotecas estándar** — sin `npm install`, sin `cargo` crates, sin
Maven. Compila y arranca sin internet.

---

## 🧠 ¿Para qué te sirve este esqueleto? (ideas de apps)

El patrón es siempre el mismo: **navegador → BFF → gateway → N servicios → agrega**.
Solo cambias *qué* calcula cada servicio:

| Si quieres construir… | Reutilizas… | Cambias… |
|------------------------|-------------|----------|
| 🖼️ **Procesador de imágenes** (miniaturas, EXIF, filtros) | Todo el esqueleto | Servicios → generan miniaturas, leen metadatos |
| 🔐 **Analizador de seguridad de archivos** (hashes, firmas, malware-ish) | Gateway + fan-out | Rust/C → hashes; Python → reglas YARA-like |
| 📊 **Dashboard de datos** (sube CSV, te da estadísticas/gráficas) | BFF + frontend + Java Core | Python → pandas-style; Java → almacena datasets |
| 🌐 **Acortador de URLs con analítica** | Gateway + Core + frontend | Rust → IDs cortos; Python → clics/stats |
| 🧪 **Banco de benchmarks** (compara lenguajes en una tarea) | Todo + las latencias que ya mide el gateway | Cada servicio → misma tarea, comparas `ms` |
| 🤖 **Orquestador de IA** (cada servicio llama a un modelo distinto) | Gateway fan-out | Servicios → wrappers de APIs de modelos |
| 📦 **Pipeline ETL** (extrae → transforma → carga) | Gateway en cadena en vez de paralelo | Encadenas servicios en secuencia |

> El gateway ya mide la **latencia de cada servicio**, así que cualquier cosa que
> construyas viene con métricas de rendimiento gratis.

---

## 🏗️ Arquitectura

```
   Navegador
      │  (sube el archivo como bytes crudos)
      ▼
  ┌─────────────────────┐   HTML/CSS + TypeScript→JS
  │  Frontend            │
  └─────────┬───────────┘
            ▼ /api/*
  ┌─────────────────────┐   Node.js  (puerto 8080)
  │  BFF                 │   sirve la web y proxea al gateway
  └─────────┬───────────┘
            ▼ /process
  ┌─────────────────────┐   Go  (puerto 8000)
  │  API Gateway        │   fan-out concurrente (goroutines) + agrega
  └──┬───┬───┬───┬───┬──┘
     │   │   │   │   └────────────► Java Core  (8001)  registro de jobs
     │   │   │   └────────────────► Python     (8002)  estadísticas
     │   │   └────────────────────► Rust       (8003)  hash SHA-256
     │   └────────────────────────► C          (8004)  checksum CRC32
     └────────────────────────────► C++        (8005)  entropía Shannon
```

| Servicio        | Lenguaje | Puerto | Qué hace | Por qué ese lenguaje |
|-----------------|----------|--------|----------|----------------------|
| `bff-node`      | Node.js  | 8080   | Sirve el frontend y proxea `/api/*` | JS en servidor, ideal como BFF |
| `gateway-go`    | Go       | 8000   | Recibe el archivo, fan-out concurrente, agrega | Goroutines = concurrencia de red |
| `core-java`     | Java     | 8001   | Guarda el historial de trabajos | Robusto para lógica de negocio |
| `analytics-python` | Python | 8002  | Tamaño, líneas, % imprimible, tipo | Rápido de escribir, bueno con datos |
| `hash-rust`     | Rust     | 8003   | SHA-256 (implementado a mano) | Velocidad + seguridad de memoria |
| `crc-c`         | C        | 8004   | CRC32 (servidor HTTP en sockets Winsock) | Lo más cercano al metal |
| `entropy-cpp`   | C++      | 8005   | Entropía de Shannon + bytes distintos | STL + rendimiento nativo |
| `frontend`      | TS→JS    | —      | Subir archivo, ver informe e historial | Tipado estático en el navegador |

La comunicación entre todos es **HTTP + JSON**. Los bytes del archivo viajan como
cuerpo crudo de la petición (sencillo de leer incluso en C).

---

## 🚀 Cómo correrlo

**Requisitos** (toolchains instaladas): Node.js, Python 3, JDK, Go, Rust (`rustc`),
GCC, G++, TypeScript (`tsc`).

```powershell
# 1) Compilar todo (Rust, C, C++, Java, Go, TypeScript)
powershell -ExecutionPolicy Bypass -File scripts\build.ps1

# 2) Arrancar los 7 procesos (cada uno en su ventana)
powershell -ExecutionPolicy Bypass -File scripts\start-all.ps1

# 3) Abrir en el navegador:  http://127.0.0.1:8080
#    Arrastra cualquier archivo y mira cómo cada servicio lo analiza.

# 4) Detener todo
powershell -ExecutionPolicy Bypass -File scripts\stop-all.ps1
```

### Probar sin navegador (curl)

```powershell
# Estado del sistema (gateway + servicios)
curl http://127.0.0.1:8000/health

# Procesar un archivo directamente contra el gateway
curl -X POST --data-binary "@README.md" -H "X-Filename: README.md" http://127.0.0.1:8000/process

# Ver el historial guardado en Java Core
curl http://127.0.0.1:8000/history
```

---

## 📂 Estructura

```
polyglot-forge/
├── frontend/            index.html, style.css, app.ts (→ app.js)
├── services/
│   ├── gateway-go/      main.go         (API Gateway)
│   ├── bff-node/        server.js       (BFF + estáticos)
│   ├── core-java/       CoreService.java
│   ├── analytics-python/analytics.py
│   ├── hash-rust/       main.rs
│   ├── crc-c/           crc_service.c
│   └── entropy-cpp/     entropy_service.cpp
├── scripts/             build.ps1, start-all.ps1, stop-all.ps1
└── README.md
```

---

## 🛠️ Cómo extenderlo (añadir tu propio servicio)

1. Crea una carpeta en `services/mi-servicio/` con un servidor HTTP que exponga
   `GET /health` y `POST /` (recibe bytes, responde JSON).
2. Regístralo en el array `processors` de [`gateway-go/main.go`](services/gateway-go/main.go).
3. Añade su compilación/arranque en `scripts/build.ps1` y `scripts/start-all.ps1`.
4. (Opcional) Dale una tarjeta en el frontend editando `LABELS` y `renderData` en
   [`frontend/app.ts`](frontend/app.ts).

El gateway hace el fan-out automáticamente: con registrarlo basta para que tu
servicio reciba cada archivo y aparezca en el informe.

---

## 🗺️ Ideas para seguir (roadmap de estudio)

- **Persistencia real** en Java Core (SQLite/JDBC) en vez de memoria.
- **Tolerancia a fallos**: reintentos y *timeouts* por servicio en el gateway.
- **Contenedores**: un `Dockerfile` por servicio + `docker-compose`.
- **WebSocket**: progreso en vivo del pipeline hacia el navegador.
- **CI**: un workflow de GitHub Actions que compile los 8 servicios en cada push.

---

## 📝 Licencia

MIT — úsalo, modifícalo y forja lo que quieras.
