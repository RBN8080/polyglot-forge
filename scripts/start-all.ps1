# start-all.ps1 — Arranca los 7 procesos del sistema, cada uno en su ventana.
# Ejecutar:  powershell -ExecutionPolicy Bypass -File scripts\start-all.ps1
#
# Orden de arranque: primero los servicios de procesamiento y el core,
# luego el gateway (Go) y por último el BFF (Node) que sirve el frontend.

$root = Split-Path -Parent $PSScriptRoot
$svc = Join-Path $root "services"

function Launch($title, $dir, $cmd) {
    Write-Host "Arrancando $title ..." -ForegroundColor Cyan
    Start-Process powershell -ArgumentList @(
        "-NoExit", "-Command",
        "`$Host.UI.RawUI.WindowTitle='$title'; Set-Location '$dir'; $cmd"
    ) | Out-Null
    Start-Sleep -Milliseconds 600
}

Launch "rust-hash:8003"   (Join-Path $svc "hash-rust")        ".\hash-rust.exe"
Launch "c-crc:8004"       (Join-Path $svc "crc-c")            ".\crc-c.exe"
Launch "cpp-entropy:8005" (Join-Path $svc "entropy-cpp")      ".\entropy-cpp.exe"
Launch "python-stats:8002" (Join-Path $svc "analytics-python") "python analytics.py"
Launch "java-core:8001"   (Join-Path $svc "core-java")        "java CoreService"

# Dar un momento a que los servicios anteriores estén escuchando.
Start-Sleep -Seconds 1
Launch "go-gateway:8000"  (Join-Path $svc "gateway-go")       ".\gateway-go.exe"

Start-Sleep -Milliseconds 800
Launch "node-bff:8080"    (Join-Path $svc "bff-node")         "node server.js"

Write-Host ""
Write-Host "Todos los servicios arrancados." -ForegroundColor Green
Write-Host "Abre el navegador en:  http://127.0.0.1:8080" -ForegroundColor Yellow
Write-Host "Para detener todo:     scripts\stop-all.ps1" -ForegroundColor Yellow
