<#
.SYNOPSIS
构建 WIFI_UART_BRIDGE，产物保存至 ESP-IDF 默认 build 目录。
#>
$ErrorActionPreference = "Stop"
$projectRoot = Split-Path -Parent $PSScriptRoot
$idfProjectRoot = Join-Path $projectRoot "src\WIFI_UART_BRIDGE"
$environmentScript = Join-Path $PSScriptRoot "esp-idf-environment.ps1"
. $environmentScript
Initialize-BridgeEspIdfEnvironment
$env:IDF_BUILD_JOBS = "1"

Push-Location $idfProjectRoot
try {
    idf.py reconfigure
    if ($LASTEXITCODE -ne 0) {
        throw "WIFI_UART_BRIDGE 配置失败，idf.py 退出码：$LASTEXITCODE"
    }

    # 仅在本机配置了全部覆盖路径时执行 Windows bootloader 预配置。
    # 普通 ESP-IDF 安装由 idf.py 自动完成该步骤。
    $hasBootloaderOverrides =
        -not [string]::IsNullOrWhiteSpace($BridgeNinjaPath) -and
        -not [string]::IsNullOrWhiteSpace($BridgeCMakePath) -and
        -not [string]::IsNullOrWhiteSpace($BridgePythonPath)
    if ($hasBootloaderOverrides) {
        foreach ($tool in @($BridgeCMakePath, $BridgePythonPath)) {
            if (-not (Test-Path -LiteralPath $tool -PathType Leaf)) {
                throw "bootloader 预配置工具不存在：$tool"
            }
        }
        $bootloaderBuild = Join-Path $idfProjectRoot "build\bootloader"
        New-Item -ItemType Directory -Path $bootloaderBuild -Force | Out-Null
        $bootloaderSource = Join-Path $env:IDF_PATH "components\bootloader\subproject"
        & $BridgeCMakePath "-G" "Ninja" "-DCMAKE_MAKE_PROGRAM=$BridgeNinjaPath" `
            "-DSDKCONFIG=$(Join-Path $idfProjectRoot 'sdkconfig')" `
            "-DIDF_PATH=$env:IDF_PATH" "-DIDF_TARGET=esp32s3" `
            "-DPYTHON_DEPS_CHECKED=1" "-DPYTHON=$BridgePythonPath" `
            "-DPROJECT_SOURCE_DIR=$idfProjectRoot" `
            "-S" $bootloaderSource "-B" $bootloaderBuild
        if ($LASTEXITCODE -ne 0) {
            throw "WIFI_UART_BRIDGE bootloader 配置失败，CMake 退出码：$LASTEXITCODE"
        }
    }

    idf.py build
    if ($LASTEXITCODE -ne 0) {
        throw "WIFI_UART_BRIDGE 构建失败，idf.py 退出码：$LASTEXITCODE"
    }
} finally {
    Pop-Location
}
