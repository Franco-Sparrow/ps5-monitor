param(
    [Parameter(Mandatory=$false)]
    [string]$Ps5Ip = "192.168.88.100"
)

$base = "http://${Ps5Ip}:9843"
$tests = @(
    "/api/v1/status",
    "/api/v1/hardware",
    "/api/v1/extended",
    "/api/v1/sensors",
    "/api/v1/cpu",
    "/api/v1/memory",
    "/api/v1/perf",
    "/api/v1/network",
    "/api/v1/storage",
    "/api/v1/volumes"
)

foreach ($path in $tests) {
    Write-Host "`n=== $path ===" -ForegroundColor Cyan
    try {
        $r = Invoke-WebRequest -UseBasicParsing -Uri ($base + $path) -TimeoutSec 8
        Write-Host "HTTP $($r.StatusCode)" -ForegroundColor Green
        $r.Content
    }
    catch {
        Write-Host $_.Exception.Message -ForegroundColor Red
    }
}
