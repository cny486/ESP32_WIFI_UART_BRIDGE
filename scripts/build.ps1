<#
.SYNOPSIS
构建 WIFI_UART_BRIDGE，产物保存至 ESP-IDF 默认 build 目录。
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
$env:IDF_CCACHE_ENABLE = "0"
$env:IDF_BUILD_JOBS = "1"
$ninjaPath = "C:\Espressif\tools\ninja\1.12.1\ninja.exe"
if (-not (Test-Path -LiteralPath $ninjaPath -PathType Leaf)) {
    throw "未找到 Ninja：$ninjaPath"
}
$ninjaDir = Split-Path -Parent $ninjaPath
$toolchainBin = "C:\Espressif\tools\xtensa-esp-elf\esp-15.2.0_20251204\xtensa-esp-elf\bin"
if (-not (Test-Path -LiteralPath $toolchainBin -PathType Container)) {
    throw "未找到 ESP32-S3 工具链：$toolchainBin"
}
$env:Path = "$ninjaDir;$toolchainBin;$env:Path"
$env:WIFI_UART_BRIDGE_NINJA_PATH = $ninjaPath.Replace("\", "/")
$env:WIFI_UART_BRIDGE_TOOLCHAIN_BIN = $toolchainBin.Replace("\", "/")
$env:CC = Join-Path $toolchainBin "xtensa-esp32s3-elf-gcc.exe"
$env:CXX = Join-Path $toolchainBin "xtensa-esp32s3-elf-g++.exe"
$env:ASM = $env:CC

Push-Location $idfProjectRoot
try {
    idf.py reconfigure
    if ($LASTEXITCODE -ne 0) {
        throw "WIFI_UART_BRIDGE 配置失败，idf.py 退出码：$LASTEXITCODE"
    }

    # ESP-IDF bootloader ExternalProject 在部分 Windows 受限环境中不会继承
    # 主工程的 CMAKE_MAKE_PROGRAM，因此显式写入同一个 Ninja 路径。
    $bootloaderBuild = Join-Path $idfProjectRoot "build\bootloader"
    New-Item -ItemType Directory -Path $bootloaderBuild -Force | Out-Null
    $cmakePath = "C:\Espressif\tools\cmake\4.0.3\bin\cmake.exe"
    $pythonPath = "C:\Espressif\tools\python\v6.1\venv\Scripts\python.exe"
    $bootloaderSource = Join-Path $env:IDF_PATH "components\bootloader\subproject"
    & $cmakePath "-G" "Ninja" "-DCMAKE_MAKE_PROGRAM=$ninjaPath" `
        "-DSDKCONFIG=$(Join-Path $idfProjectRoot 'sdkconfig')" `
        "-DIDF_PATH=$env:IDF_PATH" "-DIDF_TARGET=esp32s3" `
        "-DPYTHON_DEPS_CHECKED=1" "-DPYTHON=$pythonPath" `
        "-DPROJECT_SOURCE_DIR=$idfProjectRoot" `
        "-S" $bootloaderSource "-B" $bootloaderBuild
    if ($LASTEXITCODE -ne 0) {
        throw "WIFI_UART_BRIDGE bootloader 配置失败，CMake 退出码：$LASTEXITCODE"
    }

    idf.py build
    if ($LASTEXITCODE -ne 0) {
        throw "WIFI_UART_BRIDGE 构建失败，idf.py 退出码：$LASTEXITCODE"
    }
} finally {
    Pop-Location
}
