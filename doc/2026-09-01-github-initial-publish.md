# GitHub 初次发布

## 本次目标

将 `WIFI_UART_BRIDGE` 项目安全发布到 `https://github.com/cny486/ESP32_WIFI_UART_BRIDGE.git`，同时避免把本机 ESP-IDF 构建产物和现场 Wi-Fi 密码提交到远端。

## 涉及文件

| 文件 | 修改内容 |
| --- | --- |
| `.gitignore` | 忽略 `build/`、本机 `sdkconfig`、固件产物和编辑器文件。 |
| `README.md` | 增加 GitHub 仓库入口、外部组件依赖、配置与构建说明。 |
| `src/WIFI_UART_BRIDGE/sdkconfig.defaults` | 将现场 SSID、密码和 TCP 地址替换为无凭据的通用模板；本机 `sdkconfig` 保持不变。 |
| `scripts/publish-github.ps1` | 增加可审阅的 Git 提交与推送脚本，并在提交前检查非空 Wi-Fi 密码。 |
| `doc/README.md` | 追加本次仓库发布状态和安全配置说明。 |

## 功能与架构影响

桥接源码和运行逻辑没有变化。现有本机 `sdkconfig` 仍保存当前设备配置，因此本机后续构建不会因模板脱敏而改变；新克隆的工作区必须先通过 `scripts/configure.ps1` 设置自己的网络参数。

仓库继续通过相对路径引用 `wifi_module` 和 `uart_pt` 的唯一源码，不在本仓库复制组件实现。三个项目需要处于同一父目录，或者由使用者调整 `EXTRA_COMPONENT_DIRS`。

## 上传方式与验证

在模块根目录执行：

```powershell
.\scripts\publish-github.ps1
```

脚本只允许目标 `origin` 地址匹配时继续，不执行强制推送；提交前会阻止含非空 `CONFIG_BRIDGE_WIFI_PASSWORD` 的文件进入提交。发布后应在 GitHub 页面确认 `main` 分支存在，并确认仓库中没有 `sdkconfig` 和 `build/`。

