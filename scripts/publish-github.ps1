<#
.SYNOPSIS
提交 WIFI_UART_BRIDGE 并推送到指定 GitHub 仓库。

.DESCRIPTION
脚本会检查远端地址、暂存当前项目文件、阻止包含非空 Wi-Fi 密码的
sdkconfig 默认配置进入提交，然后在 main 分支提交并推送。不会强制推送。

.EXAMPLE
.\scripts\publish-github.ps1
#>
param(
    [string]$RemoteUrl = "https://github.com/cny486/ESP32_WIFI_UART_BRIDGE.git",
    [string]$CommitMessage = "Initial import of WIFI_UART_BRIDGE"
)

$ErrorActionPreference = "Stop"
$projectRoot = Split-Path -Parent $PSScriptRoot

function Invoke-Git {
    param([Parameter(ValueFromRemainingArguments = $true)][string[]]$Arguments)

    & git @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "git $($Arguments -join ' ') 执行失败，退出码：$LASTEXITCODE"
    }
}

Push-Location $projectRoot
try {
    Invoke-Git rev-parse --is-inside-work-tree

    $originUrl = & git remote get-url origin 2>$null
    if ($LASTEXITCODE -ne 0) {
        Invoke-Git remote add origin $RemoteUrl
    } elseif ($originUrl.Trim() -ne $RemoteUrl) {
        throw "现有 origin 指向 '$originUrl'，与目标 '$RemoteUrl' 不一致；请人工确认后再修改。"
    }

    Invoke-Git add --all

    $passwordKey = "CONFIG_BRIDGE_WIFI_" + "PASSWORD"
    $credentialPattern = $passwordKey + '="[^"]+"'
    $credentialMatch = & git grep --cached -n -E $credentialPattern -- 2>$null
    if ($LASTEXITCODE -eq 0) {
        throw "检测到待提交文件中包含非空 Wi-Fi 密码：`n$credentialMatch`n请先移除凭据再上传。"
    }
    if ($LASTEXITCODE -ne 1) {
        throw "凭据检查失败，git grep 退出码：$LASTEXITCODE"
    }

    & git diff --cached --quiet
    $hasChanges = $LASTEXITCODE -ne 0
    if ($hasChanges) {
        Invoke-Git commit -m $CommitMessage
    } else {
        Write-Host "没有需要提交的新变更，将直接推送现有 main 分支。"
    }

    Invoke-Git branch -M main
    Invoke-Git push --set-upstream origin main
} finally {
    Pop-Location
}
