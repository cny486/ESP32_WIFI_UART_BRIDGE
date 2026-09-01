<#
.SYNOPSIS
打开 WIFI_UART_BRIDGE 的 ESP-IDF menuconfig 配置界面。
#>
$ErrorActionPreference = "Stop"
$projectRoot = Split-Path -Parent $PSScriptRoot
$idfProjectRoot = Join-Path $projectRoot "src\WIFI_UART_BRIDGE"
$environmentScript = Join-Path $PSScriptRoot "esp-idf-environment.ps1"
. $environmentScript
Initialize-BridgeEspIdfEnvironment

Push-Location $idfProjectRoot
try {
    idf.py menuconfig
    if ($LASTEXITCODE -ne 0) {
        throw "WIFI_UART_BRIDGE 配置界面异常退出，idf.py 退出码：$LASTEXITCODE"
    }
} finally {
    Pop-Location
}
