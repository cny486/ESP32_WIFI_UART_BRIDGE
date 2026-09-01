<#
.SYNOPSIS
清理 WIFI_UART_BRIDGE 的 ESP-IDF 默认 build 构建产物。
#>
$ErrorActionPreference = "Stop"
$projectRoot = Split-Path -Parent $PSScriptRoot
$idfProjectRoot = Join-Path $projectRoot "src\WIFI_UART_BRIDGE"
$idfProfile = "C:\Espressif\tools\Microsoft.v6.1.PowerShell_profile.ps1"

if (-not (Test-Path -LiteralPath $idfProfile -PathType Leaf)) {
    throw "未找到 ESP-IDF v6.1 PowerShell 配置：$idfProfile"
}

. $idfProfile
$env:PYTHONUTF8 = "1"

Push-Location $idfProjectRoot
try {
    idf.py fullclean
    if ($LASTEXITCODE -ne 0) {
        throw "WIFI_UART_BRIDGE 清理失败，idf.py 退出码：$LASTEXITCODE"
    }
} finally {
    Pop-Location
}
