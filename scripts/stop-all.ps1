# stop-all.ps1 — Detiene todos los servicios del pipeline.
# Mata los procesos por los puertos que ocupan (8000-8005, 8080).

$ports = 8000, 8001, 8002, 8003, 8004, 8005, 8080

Write-Host "Deteniendo servicios del pipeline..." -ForegroundColor Cyan

foreach ($port in $ports) {
    try {
        $conns = Get-NetTCPConnection -LocalPort $port -State Listen -ErrorAction Stop
        foreach ($c in $conns) {
            $procId = $c.OwningProcess
            $name = (Get-Process -Id $procId -ErrorAction SilentlyContinue).ProcessName
            Stop-Process -Id $procId -Force -ErrorAction SilentlyContinue
            Write-Host "    Puerto $port -> detenido ($name, PID $procId)" -ForegroundColor Green
        }
    } catch {
        Write-Host "    Puerto $port -> nada escuchando" -ForegroundColor DarkGray
    }
}

Write-Host "Listo." -ForegroundColor Green
