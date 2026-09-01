<#
.SYNOPSIS
保存当前宿主机的 ESP-IDF 安装路径。

.EXAMPLE
.\scripts\set-esp-idf-paths.ps1 `
  -EspIdfProfile "C:\path\to\ESP-IDF-PowerShell-profile.ps1"

.DESCRIPTION
生成 scripts/esp-idf.paths.local.ps1。该文件被 Git 忽略，不会把个人安装
路径提交到仓库。Ninja、工具链、CMake 和 Python 路径均为可选覆盖项。
#>
param(
    [Parameter(Mandatory = $true)][string]$EspIdfProfile,
    [string]$NinjaPath = "",
    [string]$ToolchainBin = "",
    [string]$CMakePath = "",
    [string]$PythonPath = ""
)

$ErrorActionPreference = "Stop"

function Assert-ConfiguredPath {
    param([string]$Label, [string]$Path, [switch]$Container)

    if ([string]::IsNullOrWhiteSpace($Path)) {
        return
    }
    $pathType = if ($Container) { "Container" } else { "Leaf" }
    if (-not (Test-Path -LiteralPath $Path -PathType $pathType)) {
        throw "$Label 路径不存在：$Path"
    }
}

Assert-ConfiguredPath "ESP-IDF profile" $EspIdfProfile
Assert-ConfiguredPath "Ninja" $NinjaPath
Assert-ConfiguredPath "ESP32-S3 工具链" $ToolchainBin -Container
Assert-ConfiguredPath "CMake" $CMakePath
Assert-ConfiguredPath "Python" $PythonPath

function ConvertTo-SingleQuotedLiteral {
    param([string]$Value)
    return "'" + $Value.Replace("'", "''") + "'"
}

$localPathsFile = Join-Path $PSScriptRoot "esp-idf.paths.local.ps1"
$lines = @(
    "# 由 set-esp-idf-paths.ps1 生成；仅供当前宿主机使用。",
    "`$BridgeEspIdfProfile = $(ConvertTo-SingleQuotedLiteral $EspIdfProfile)",
    "`$BridgeNinjaPath = $(ConvertTo-SingleQuotedLiteral $NinjaPath)",
    "`$BridgeToolchainBin = $(ConvertTo-SingleQuotedLiteral $ToolchainBin)",
    "`$BridgeCMakePath = $(ConvertTo-SingleQuotedLiteral $CMakePath)",
    "`$BridgePythonPath = $(ConvertTo-SingleQuotedLiteral $PythonPath)"
)
Set-Content -LiteralPath $localPathsFile -Value $lines -Encoding utf8
Write-Host "已保存本机 ESP-IDF 路径：$localPathsFile"
