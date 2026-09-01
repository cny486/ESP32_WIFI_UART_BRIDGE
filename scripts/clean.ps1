<#
.SYNOPSIS
清理 WIFI_UART_BRIDGE 的 ESP-IDF 默认 build 构建产物。
#>
$ErrorActionPreference = "Stop"
$projectRoot = Split-Path -Parent $PSScriptRoot
$idfProjectRoot = Join-Path $projectRoot "src\WIFI_UART_BRIDGE"
$environmentScript = Join-Path $PSScriptRoot "esp-idf-environment.ps1"
. $environmentScript
Initialize-BridgeEspIdfEnvironment

Push-Location $idfProjectRoot
try {
    idf.py fullclean
    if ($LASTEXITCODE -ne 0) {
        throw "WIFI_UART_BRIDGE 清理失败，idf.py 退出码：$LASTEXITCODE"
    }
} finally {
    Pop-Location
}
