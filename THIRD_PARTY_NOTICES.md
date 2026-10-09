# 第三方组件与许可声明

本文件列出 McServerManager（以下称"本项目"）分发物中包含的第三方组件及其许可条款。
本项目自身源码采用 [MIT 许可](LICENSE)。

## Qt 6

* 使用模块：Core、Gui、Widgets、Network、Qml（QJSEngine）
* 许可：**Qt 个人版 / 开源条款**。本项目按 Qt 官方"个人使用（开源）"路径使用 Qt，
  对应 **GPLv3 或 LGPLv3**（详见 <https://www.qt.io/licensing>）。
* 使用方式：**动态链接**，未修改 Qt 源码，也未静态链接 Qt。
  发行包内的 `Qt6Core.dll`、`Qt6Gui.dll`、`Qt6Widgets.dll`、`Qt6Network.dll`、
  `Qt6Qml*.dll`、`platforms/`、`imageformats/`、`styles/`、`tls/` 等即 Qt 运行库，
  用户可以自行替换为与其兼容的 Qt 版本（LGPLv3 要求的可替换性）。
* Qt 源码获取：<https://download.qt.io/> 或 <https://code.qt.io/>。
* 若你需要闭源 / 商业分发，请自行购买 Qt 商业许可，或遵守 GPLv3 的完整开源义务。

## 7-Zip（内置 `7za.exe`）

* 位置：`third_party/7zip/`，发行包内为 `7zip/7za.exe`
* 版本：7-Zip 26.04 Extra（`7za.exe` 独立命令行版本）
* 许可：**GNU LGPL**，并带有 unRAR 限制条款（详见随附的
  [`LICENSE-7zip.txt`](third_party/7zip/LICENSE-7zip.txt)）。
* 用途：解压 / 校验**管理器插件包**（zip）。本项目未修改其源码，仅原样随包分发可执行文件。
* 源码获取：<https://www.7-zip.org/> 与 <https://github.com/ip7z/7zip>。

## Docker 镜像（运行时下载，不在本项目分发物内）

* 本项目在创建服务器时按 Minecraft 版本拉取 `eclipse-temurin:<版本>-jre` 镜像，
  该镜像由 Eclipse Adoptium 项目提供，许可为 **GPLv2 + Classpath Exception**。
* 镜像不随本项目分发，由 Docker 在用户机器上拉取。

## Minecraft 相关

* 本项目与 Mojang Studios / Microsoft 无任何关联。
* "Minecraft" 是 Mojang AB 的商标。服务端 jar（Paper / Purpur / 原版 / Fabric）由各自的
  官方渠道下载，遵循其各自的许可；使用本项目创建服务器即表示你同意
  [Minecraft EULA](https://aka.ms/MinecraftEULA)。
* 通过插件市场（Modrinth / Hangar / SpigotMC）下载的 Minecraft 插件遵循其作者声明的许可。

## 管理器插件包（Plugin SDK）

* 管理器插件由第三方作者提供，授权条款由插件作者在 `plugin.json` 的 `license` 字段中声明。
* 本项目只提供加载机制与接口，不对第三方插件的授权、行为与安全性负责；
  安装前请确认来源（详见 [docs/PLUGIN-SDK.md](docs/PLUGIN-SDK.md) 第 8 节）。

---

## 关于 AI 辅助开发

本项目的源码、界面文案与文档在开发过程中大量使用了 **AI 辅助**（代码生成、重构建议、
问题排查、文档撰写），由维护者审核、测试并整合后提交。
所有检查脚本都在仓库里公开，可自行复现验证：

```bash
python tools/static_check.py                 # 源码一致性检查
powershell -File tools/smoke-test.ps1        # 后端冒烟测试（67 项，含安全用例）
powershell -File tools/gui-smoke.ps1         # 界面冒烟测试
powershell -File tools/ui-perf.ps1           # 界面 CPU 测量
```
