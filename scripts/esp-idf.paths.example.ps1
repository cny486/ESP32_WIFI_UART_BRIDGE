<#
复制本文件为 esp-idf.paths.local.ps1，填写宿主机上的实际安装路径。
也可以使用 set-esp-idf-paths.ps1 自动生成本地配置。

只有 $BridgeEspIdfProfile 是未提前激活 ESP-IDF 环境时的必填项。
其它路径通常可由 ESP-IDF profile 自动配置；仅在自动发现失败时填写。
#>
$BridgeEspIdfProfile = "C:\path\to\ESP-IDF-PowerShell-profile.ps1"
$BridgeNinjaPath = ""
$BridgeToolchainBin = ""
$BridgeCMakePath = ""
$BridgePythonPath = ""

