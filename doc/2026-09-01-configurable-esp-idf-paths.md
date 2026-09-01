# 可配置的宿主机 ESP-IDF 路径

## 本次目标

ESP-IDF 和交叉编译工具链继续由使用者安装在宿主机，但项目脚本不再写死当前开发机路径。每台机器保存自己的本地路径配置，仓库只提交模板、加载逻辑和配置生成脚本。

## 涉及文件

| 文件 | 修改内容 |
| --- | --- |
| `.gitignore` | 忽略 `scripts/esp-idf.paths.local.ps1`，避免提交个人安装路径。 |
| `scripts/esp-idf.paths.example.ps1` | 新增可提交的路径字段模板。 |
| `scripts/set-esp-idf-paths.ps1` | 验证 ESP-IDF profile 及可选工具路径，生成当前宿主机配置。 |
| `scripts/esp-idf-environment.ps1` | 统一加载本机配置或复用已激活的 ESP-IDF 环境。 |
| `scripts/build.ps1` | 删除固定安装路径，改用统一环境加载器；完整覆盖路径存在时保留 Windows bootloader 预配置。 |
| `scripts/configure.ps1` | 删除固定安装路径，改用统一环境加载器。 |
| `scripts/clean.ps1` | 删除固定安装路径，改用统一环境加载器。 |
| `README.md`、`doc/README.md` | 增加首次路径配置、可选覆盖项和构建使用说明。 |

## 功能与架构影响

桥接固件、组件源码和 ESP-IDF CMake 结构没有变化。脚本层新增单一环境入口：三个操作脚本都调用 `Initialize-BridgeEspIdfEnvironment`，避免分别维护工具路径。

环境加载顺序如下：

1. 如果存在被 Git 忽略的 `esp-idf.paths.local.ps1`，先读取本机配置。
2. 如果当前终端已经能找到 `idf.py`，直接复用当前 ESP-IDF 环境。
3. 否则加载本机配置中的 ESP-IDF PowerShell profile。
4. 校验 `idf.py` 和 `IDF_PATH`；对已填写的 Ninja、工具链、CMake、Python 路径进行存在性检查。

## 使用方式

普通安装通常只需指定 ESP-IDF profile：

```powershell
.\scripts\set-esp-idf-paths.ps1 `
  -EspIdfProfile "C:\你的安装目录\ESP-IDF-PowerShell-profile.ps1"
```

遇到工具自动发现问题时，可以追加：

```powershell
-NinjaPath "C:\path\to\ninja.exe" `
-ToolchainBin "C:\path\to\xtensa-esp-elf\bin" `
-CMakePath "C:\path\to\cmake.exe" `
-PythonPath "C:\path\to\python.exe"
```

随后执行 `scripts/configure.ps1`、`scripts/build.ps1` 或 `scripts/clean.ps1`。本机路径文件可重新运行配置脚本覆盖生成。

## 构建与验证

所有 PowerShell 脚本已完成语法解析检查。本机通过 `set-esp-idf-paths.ps1` 生成完整路径配置，确认该文件被 Git 忽略，再使用改造后的 `scripts/build.ps1` 完成 ESP-IDF v6.1、ESP32-S3 全量构建。仓库内 `wifi_module`、`uart_pt` 继续正常链接，生成固件 `WIFI_UART_BRIDGE.bin`，大小为 `0xc6c60` 字节，默认 app 分区剩余 `0x393a0` 字节（22%）。硬件烧录与通信验证不在本次脚本改造范围内。

