function Initialize-BridgeEspIdfEnvironment {
    $BridgeEspIdfProfile = ""
    $BridgeNinjaPath = ""
    $BridgeToolchainBin = ""
    $BridgeCMakePath = ""
    $BridgePythonPath = ""
    $localPathsFile = Join-Path $PSScriptRoot "esp-idf.paths.local.ps1"
    if (Test-Path -LiteralPath $localPathsFile -PathType Leaf) {
        . $localPathsFile
    }

    $idfCommand = Get-Command idf.py -ErrorAction SilentlyContinue
    if ($null -eq $idfCommand) {
        if ([string]::IsNullOrWhiteSpace($BridgeEspIdfProfile)) {
            throw @"
当前终端尚未激活 ESP-IDF，也没有本机路径配置。
请先运行 scripts/set-esp-idf-paths.ps1，或复制
scripts/esp-idf.paths.example.ps1 为 scripts/esp-idf.paths.local.ps1 并填写路径。
"@
        }
        if (-not (Test-Path -LiteralPath $BridgeEspIdfProfile -PathType Leaf)) {
            throw "未找到 ESP-IDF PowerShell profile：$BridgeEspIdfProfile"
        }
        . $BridgeEspIdfProfile
    }

    if ($null -eq (Get-Command idf.py -ErrorAction SilentlyContinue)) {
        throw "ESP-IDF 环境初始化后仍未找到 idf.py，请检查 profile 和 ESP-IDF 安装。"
    }
    if ([string]::IsNullOrWhiteSpace($env:IDF_PATH) -or
        -not (Test-Path -LiteralPath $env:IDF_PATH -PathType Container)) {
        throw "IDF_PATH 未设置或目录不存在，请检查 ESP-IDF 环境。"
    }

    $pathEntries = [System.Collections.Generic.List[string]]::new()
    if (-not [string]::IsNullOrWhiteSpace($BridgeNinjaPath)) {
        if (-not (Test-Path -LiteralPath $BridgeNinjaPath -PathType Leaf)) {
            throw "未找到配置的 Ninja：$BridgeNinjaPath"
        }
        $pathEntries.Add((Split-Path -Parent $BridgeNinjaPath))
        $env:WIFI_UART_BRIDGE_NINJA_PATH = $BridgeNinjaPath.Replace("\", "/")
    }
    if (-not [string]::IsNullOrWhiteSpace($BridgeToolchainBin)) {
        if (-not (Test-Path -LiteralPath $BridgeToolchainBin -PathType Container)) {
            throw "未找到配置的 ESP32-S3 工具链目录：$BridgeToolchainBin"
        }
        $pathEntries.Add($BridgeToolchainBin)
        $env:WIFI_UART_BRIDGE_TOOLCHAIN_BIN = $BridgeToolchainBin.Replace("\", "/")
        $env:CC = Join-Path $BridgeToolchainBin "xtensa-esp32s3-elf-gcc.exe"
        $env:CXX = Join-Path $BridgeToolchainBin "xtensa-esp32s3-elf-g++.exe"
        $env:ASM = $env:CC
    }
    if ($pathEntries.Count -gt 0) {
        $env:Path = "$($pathEntries -join ';');$env:Path"
    }

    $env:PYTHONUTF8 = "1"
    $env:IDF_CCACHE_ENABLE = "0"

    # 供 build.ps1 判断是否需要本机专用的 bootloader 预配置。
    $script:BridgeNinjaPath = $BridgeNinjaPath
    $script:BridgeToolchainBin = $BridgeToolchainBin
    $script:BridgeCMakePath = $BridgeCMakePath
    $script:BridgePythonPath = $BridgePythonPath
}
