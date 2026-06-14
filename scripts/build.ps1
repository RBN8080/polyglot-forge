# build.ps1 — Compila todos los servicios que necesitan compilación.
# Ejecutar desde cualquier sitio:  powershell -ExecutionPolicy Bypass -File scripts\build.ps1
#
# Compila: Rust, C, C++, Java, Go y el frontend TypeScript.
# Python y Node se ejecutan directamente (no requieren compilación).

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$svc = Join-Path $root "services"

function Step($msg) { Write-Host "==> $msg" -ForegroundColor Cyan }
function Ok($msg)   { Write-Host "    [OK] $msg" -ForegroundColor Green }
function Fail($msg) { Write-Host "    [FALLO] $msg" -ForegroundColor Red }

$failed = $false

# --- Rust (SHA-256) ---
Step "Rust: hash-rust (SHA-256)"
Push-Location (Join-Path $svc "hash-rust")
rustc main.rs -O -o hash-rust.exe
if ($LASTEXITCODE -eq 0) { Ok "hash-rust.exe" } else { Fail "rustc"; $failed = $true }
Pop-Location

# --- C (CRC32, Winsock) ---
Step "C: crc-c (CRC32)"
Push-Location (Join-Path $svc "crc-c")
gcc crc_service.c -O2 -o crc-c.exe -lws2_32
if ($LASTEXITCODE -eq 0) { Ok "crc-c.exe" } else { Fail "gcc"; $failed = $true }
Pop-Location

# --- C++ (entropía, Winsock) ---
Step "C++: entropy-cpp (entropia Shannon)"
Push-Location (Join-Path $svc "entropy-cpp")
g++ entropy_service.cpp -O2 -std=c++17 -o entropy-cpp.exe -lws2_32
if ($LASTEXITCODE -eq 0) { Ok "entropy-cpp.exe" } else { Fail "g++"; $failed = $true }
Pop-Location

# --- Java (Core) ---
Step "Java: core-java (registro de jobs)"
Push-Location (Join-Path $svc "core-java")
javac CoreService.java
if ($LASTEXITCODE -eq 0) { Ok "CoreService.class" } else { Fail "javac"; $failed = $true }
Pop-Location

# --- Go (Gateway) ---
Step "Go: gateway-go (API Gateway)"
Push-Location (Join-Path $svc "gateway-go")
go build -o gateway-go.exe .
if ($LASTEXITCODE -eq 0) { Ok "gateway-go.exe" } else { Fail "go build"; $failed = $true }
Pop-Location

# --- Frontend (TypeScript -> JS) ---
Step "TypeScript: frontend (app.ts -> app.js)"
Push-Location (Join-Path $root "frontend")
tsc app.ts --target ES2017 --lib "ES2017,DOM" --strict
if ($LASTEXITCODE -eq 0) { Ok "app.js" } else { Fail "tsc"; $failed = $true }
Pop-Location

Write-Host ""
if ($failed) {
    Write-Host "BUILD CON ERRORES. Revisa los mensajes de arriba." -ForegroundColor Red
    exit 1
} else {
    Write-Host "BUILD COMPLETO. Ahora ejecuta: scripts\start-all.ps1" -ForegroundColor Green
}
