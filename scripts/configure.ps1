<#
.SYNOPSIS
打开 WIFI_UART_BRIDGE 的 ESP-IDF menuconfig 配置界面。
#>
$ErrorActionPreference = "Stop"
$projectRoot = Split-Path -Parent $PSScriptRoot
$idfProjectRoot = Join-Path $projectRoot "src\WIFI_UART_BRIDGE"
$idfProfile = "C:\Espressif\tools\Microsoft.v6.1.PowerShell_profile.ps1"
$ninjaPath = "C:\Espressif\tools\ninja\1.12.1\ninja.exe"
$toolchainBin = "C:\Espressif\tools\xtensa-esp-elf\esp-15.2.0_20251204\xtensa-esp-elf\bin"

if (-not (Test-Path -LiteralPath $idfProfile -PathType Leaf)) {
    throw "未找到 ESP-IDF v6.1 PowerShell 配置：$idfProfile"
}
if (-not (Test-Path -LiteralPath $ninjaPath -PathType Leaf)) {
    throw "未找到 Ninja：$ninjaPath"
}
if (-not (Test-Path -LiteralPath $toolchainBin -PathType Container)) {
    throw "未找到 ESP32-S3 工具链：$toolchainBin"
}

. $idfProfile
$env:PYTHONUTF8 = "1"
$env:IDF_CCACHE_ENABLE = "0"
$env:Path = "$(Split-Path -Parent $ninjaPath);$toolchainBin;$env:Path"
$env:WIFI_UART_BRIDGE_NINJA_PATH = $ninjaPath.Replace("\", "/")
$env:WIFI_UART_BRIDGE_TOOLCHAIN_BIN = $toolchainBin.Replace("\", "/")

Push-Location $idfProjectRoot
try {
    idf.py menuconfig
    if ($LASTEXITCODE -ne 0) {
        throw "WIFI_UART_BRIDGE 配置界面异常退出，idf.py 退出码：$LASTEXITCODE"
    }
} finally {
    Pop-Location
}
