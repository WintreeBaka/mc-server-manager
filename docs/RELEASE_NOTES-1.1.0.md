# McServerManager 1.1.0

1.1.0 的主题是**管理器插件（Plugin SDK）**：管理器本身现在也能被插件扩展 ——
加界面、加后端能力、加本地 Web 运维面板。同时修掉了设置页在小窗口下的布局问题，
并让「纯程序版」与「带示例插件版」不再重名。

## 下载

| 文件 | 说明 |
| --- | --- |
| `McServerManager-1.1.0-win64.zip` | Windows 10/11 64 位便携版，解压即用，**不需要安装 Qt 或 Java** |
| `PLUGIN-SDK.md` | 插件接口说明文档（单独附件，也随包放在 `docs/`） |

> 使用前请先安装并启动 [Docker Desktop](https://www.docker.com/products/docker-desktop/)。
> 国内网络建议在 Docker 设置里配置镜像加速（`registry-mirrors`），否则拉取 JDK 镜像会很慢。

解压后目录结构：

```
McServerManager.exe     桌面界面
mcsm-cli.exe            后端 CLI（界面会自动找到同目录的这一份）
Qt6*.dll, platforms/    运行库，勿删除
7zip/7za.exe            内置 7-Zip：用于解压管理器插件包（含 LICENSE）
docs/                   插件接口文档等
examples/plugins/       三个示例插件的源码 + 打包脚本
QUICKSTART.txt          中文上手指南
README.md               完整文档
```

## 本版新增

### 管理器插件（Plugin SDK）

- **导入**：设置 → 插件扩展 → 添加插件，支持**本地 zip** 与 **URL**；
  导入前先校验并把识别结果（前端扩展 / 后端服务 / Web 支持库 / 全局）展示给用户。
- **自动识别作用域**：读 `plugin.json` 的 `scope`，缺失时按 `frontend/`、`backend/`、`web/` 目录结构推断；
  入口文件缺失、`apiVersion` 主版本不符等会直接拒绝导入并说明原因。
- **前端扩展接口**：插件脚本跑在 QJSEngine 里，通过 `mcsm.*` 注册页面
  （`heading / text / keyvalue / table / buttons / log / html / divider / progress` 区块）、
  弹通知、读写插件存储、订阅事件、调用任意后端命令；插件页面自动出现在左侧菜单，
  主题 / 字体 / 配色跟随用户设置。
- **后端扩展接口**：独立进程 + JSON-Line 协议，支持 `node` / `python` / `java` / `exec` / `auto` 运行时与超时保护；
  钩子覆盖 `plugin.install|enable|disable|uninstall`、
  `server.beforeStart|afterStart|beforeStop|afterStop|beforeBackup|afterBackup|configApplied`、`scheduler.tick`。
- **性能优化类插件**：`server.beforeStart` 返回补丁（`jvmArgs` / `env` / `dockerArgs` / `note`，
  或 `cancel` + `reason` 阻止启动），合并进容器 `JAVA_OPTS`、容器参数与生成的启动脚本；
  落盘在 `<服务器目录>/.mcsm/start-patch.json`，停用插件后自动清除。
- **支持库 / Web 面板类插件**：`plugin service start|stop|status` 管理插件自带的本地 HTTP 服务
  （注入 `MCSM_PLUGIN_ID` / `MCSM_PLUGIN_DIR` / `MCSM_PLUGIN_DATA` / `MCSM_PLUGIN_PORT`），
  界面上「打开面板」一键启动并打开浏览器。
- **内置 7-Zip**：解压插件包不依赖目标机器安装压缩软件（`third_party/7zip/7za.exe`，含许可文件）。
- **示例插件**：`examples/plugins/` 提供前端（hello-panel）、后端（perf-tuner）、全局（ops-console）三个可直接安装的示例。
- **接口文档**：[`docs/PLUGIN-SDK.md`](PLUGIN-SDK.md)，并可用 `mcsm-cli plugin api` 导出机器可读清单。

### 界面

- 设置页左侧菜单改为**固定列**：不再随内容滚动，实验性功能开关与「返回主页」始终贴在窗口底部；
  内容再长也只需要滚动右侧区域。
- 左侧菜单「插件」改名为 **「服务器插件」**，与「设置 → 插件扩展」明确区分。
- 「本机 JDK」从实验性页面移到 **设置 → 运行环境**（标注为实验性，开关关闭时只显示提示）；
  实验性页面暂时保留为空白页，留给后续实验性能力。
- 深链接新增 `--page settings-appearance | settings-plugins | settings-runtime | settings-experimental`，
  也可以直接写插件注册的页面 id（例如 `--page hello-panel`）。

### 打包与测试

- 发行版按风味区分名字，避免互相覆盖：
  `McServerManager-1.1.0-win64`（纯程序）与 `McServerManager-1.1.0-win64-with-plugins`
  （额外带 `plugin-packages/`，内含三个示例插件 zip，可直接导入测试）。
- 后端冒烟测试扩展到 **56 项**，新增插件包回归：现场打包前端 / 后端 / 全局三种插件，
  覆盖「校验识别 → 安装 → 重复安装拒绝 → `--force` 覆盖 → 列表 → 后端 JSON-Line 调用 → 启停 → 卸载 → 非法包拒绝」。
- 界面冒烟测试新增 `-Resize WxH`（小窗口布局回归）与插件页面截图。

## 本版修复

- 设置页左侧菜单会随内容一起滚动，导致「返回主页 / 实验性功能」要翻很久才能看到（改为固定列）。
- 数据根目录（`--home` / `MCSM_HOME`）始终解析为绝对路径，插件注册表与安装路径不再受当前工作目录影响。
- 插件包解压时相对路径计算错误，曾把文件复制到错误的子目录。
- `Json::parseObject` 兼容带 UTF-8 BOM 的 JSON（Windows 记事本 / PowerShell 写出的 `plugin.json` 可直接使用）。

## 升级说明

- 直接从 1.0.0 覆盖升级即可，数据目录（`%LOCALAPPDATA%\McServerManager`）与已创建的服务器都不受影响。
- 本版起需要 Qt **Qml** 模块才能自行编译（`QJSEngine` 是插件宿主的前端运行时）；
  `scripts/build-windows.ps1` 与 README 里的构建步骤均已更新。
- 只想要程序本体时下载 `McServerManager-1.1.0-win64.zip`；想在本地试插件就下载带 `-with-plugins` 的那一份，
  然后在「设置 → 插件扩展 → 添加插件」里导入 `plugin-packages/` 里的 zip。

## 已知边界

- 管理器插件是**受信任扩展**（不是沙箱），安装第三方插件前请确认来源。
- 前端插件脚本运行在 QJSEngine 中，没有 DOM、没有 `require`（可用 `mcsm.include()` 拆分脚本）。
- 后端插件需要对应的运行时（Node / Python / Java）在 `PATH` 中；缺失时界面会给出提示。
- 定时备份仍依赖应用运行（启动时会拉起 `mcsm-cli daemon`）。
