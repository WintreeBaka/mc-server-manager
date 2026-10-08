# 更新记录

## 1.1.0 — 2026-10-08

新增**管理器插件（Plugin SDK）**：给 McServerManager 本身加界面、加后端能力、加 Web 运维面板。
完整接口文档见 [`docs/PLUGIN-SDK.md`](docs/PLUGIN-SDK.md)。

**插件系统**

- 插件包导入：设置 → 插件扩展 → 添加插件，支持**本地 zip** 与 **URL** 两种来源；命令行等价入口
  `mcsm-cli plugin package install --zip|--url`
- 内置 **7-Zip**（`third_party/7zip/7za.exe`）：解压插件包不依赖目标机器安装任何压缩软件
- **作用域自动识别**：`plugin.json` 的 `scope`，或按 `frontend/` / `backend/` / `web/` 目录结构推断出
  `frontend` / `backend` / `web` / `global`；导入前先校验并把识别结果展示给用户
- **前端扩展接口**：QJSEngine 宿主 + `mcsm.*` API（页面注册、区块化界面、Toast、存储、设置、
  服务器列表、`backend.invoke` 调用任意后端命令、事件订阅、`include` 拆分脚本），插件页面自动出现在左侧菜单
- **后端扩展接口**：独立进程 + JSON-Line 协议，支持 `node` / `python` / `java` / `exec` / `auto` 运行时与超时保护；
  钩子覆盖 `plugin.install|enable|disable|uninstall`、
  `server.beforeStart|afterStart|beforeStop|afterStop|beforeBackup|afterBackup|configApplied`、`scheduler.tick`
- **后端性能优化类插件**：`server.beforeStart` 返回启动补丁（`jvmArgs` / `env` / `dockerArgs` / `note`，
  或用 `cancel` + `reason` 阻止启动），合并写入 `<服务器目录>/.mcsm/start-patch.json`，
  同时作用于容器参数与生成的启动脚本；停用插件后补丁自动清除
- **支持库 / Web 面板类插件**：`plugin service start|stop|status` 管理插件自带的本地 HTTP 服务
  （注入 `MCSM_PLUGIN_ID` / `MCSM_PLUGIN_DIR` / `MCSM_PLUGIN_DATA` / `MCSM_PLUGIN_PORT`），
  界面「打开面板」按钮一键启动并打开浏览器
- 示例插件：`examples/plugins/`（前端 `hello-panel`、后端 `perf-tuner`、全局 `ops-console`）+ 打包脚本
- 接口文档：`docs/PLUGIN-SDK.md`，可用 `mcsm-cli plugin api` 导出机器可读清单

**界面与交互**

- 设置页新增「插件扩展」分类（非实验性功能）：插件列表带作用域标签、启用开关、卸载、
  打开页面 / 打开面板 / 打开插件目录 / 接口文档
- 插件注册的页面会追加到左侧主菜单，沿用原有页面切换动画与主题
- 界面深链接新增 `--page settings-plugins` / `extensions` / `settings-runtime`，并支持直接写插件页面 id
- 设置页左侧菜单改为**固定列**（新增 `PageBase::setSideColumn()`）：菜单不再随内容滚动，
  实验性功能开关与「返回主页」固定在窗口底部，长页面只滚动右侧内容
- 左侧菜单「插件」改名为「服务器插件」，与「设置 → 插件扩展」区分；该页副标题也做了说明
- 「本机 JDK」从实验性页面移到「设置 → 运行环境」（标注实验性，开关关闭时只显示提示）；
  实验性页面暂时留空，预留给后续实验性能力
- 深链接支持 `settings-appearance` / `settings-experimental` 等全部设置分页

**修复与改进**

- 数据根目录（`--home` / `MCSM_HOME`）始终解析为绝对路径，插件注册表与安装路径不再受当前工作目录影响
- 插件包解压时的相对路径计算错误，曾导致文件被复制到错误的子目录
- `Json::parseObject` 现在兼容带 UTF-8 BOM 的 JSON（Windows 记事本 / PowerShell 写出的 `plugin.json` 可直接使用）
- 发行版两种风味改为不同名字（`-win64` 与 `-win64-with-plugins`），
  重新生成纯程序版时不再覆盖带示例插件的测试包；`package-release.ps1` 新增 `-Flavor` 参数
- `gui-smoke.ps1` 新增 `-Resize WxH`，用于小窗口下的布局回归（菜单被滚走、卡片重叠这类问题）

**验证**

- 后端冒烟测试扩展到 **56 项**，新增插件包回归：现场打包前端 / 后端 / 全局三种插件，
  覆盖「校验识别 → 安装 → 重复安装拒绝 → `--force` 覆盖 → 列表 → 后端 JSON-Line 调用 → 启停 → 卸载 → 非法包拒绝」
- 容器模式实测：`docker inspect` 确认插件写入的 JVM 参数进入容器 `JAVA_OPTS`，启动日志出现 `[mcsm] plugin tuning: com.example.perf-tuner`
- 本机 JDK 模式实测：生成的 `start.cmd` 含插件参数，停用插件后补丁文件被删除、参数消失
- 便携版（解压后仅系统 `PATH`）实测可完成插件安装与 `doctor` 自检
- 小窗口（1100×700 / 1080×700）实测三个设置分页：左侧菜单完整可见，
  实验性功能开关与「返回主页」位置一致，右侧内容独立滚动

## 1.0.0 — 2026-10-03

首个完整版本：Qt 6 桌面端 + `mcsm-cli` 后端 + Docker 化 JDK 运行环境。

**核心功能**

- 一键自动配置（Paper / Purpur / 原版 / Fabric）与手动导入两种模式
- 服务器生命周期管理：启动 / 停止 / 重启 / 强制停止 / 状态与资源占用
- 控制台：实时日志跟随（按等级着色）+ RCON 指令（含常用快捷指令）
- 配置文件：快捷表单与专家模式双模式编辑，保留注释，自动留档、可精确回滚
- 备份：定时自动备份（间隔 / 保留份数 / 含插件 / 备份前存档）、手动快照、一键恢复
- 插件市场：Modrinth / Hangar 搜索与安装，Spigot 搜索；支持启停与删除
- 界面：暗黑 / 亮色主题，马卡龙粉-蓝配色，渐变与纯色两种风格，贝塞尔缓动动画与速度调节，多种字体预设与自定义字体
- 实验性功能：扫描本机 JDK，创建服务器时可克隆本机 JDK 直接运行（不经过容器）

**开发过程中修复的主要问题**

- PaperMC 旧接口（`api.papermc.io/v2`）返回 410，迁移到新的 `fill.papermc.io/v3`
- Minecraft 改用年份版本号后（26.x 需要 Java 25），JDK 版本推导规则更新；原版改为读取 Mojang 元数据
- RCON 客户端两处缺陷（报文长度计算、`waitForReadyRead` 语义），修复前控制台指令完全不可用
- RCON 端口未映射到宿主机，容器内监听但主机无法连接
- 启动失败检测漏判：服务端损坏时容器被重启策略反复拉起，曾被误判为“启动成功”
- `server.properties` 生成时出现重复键（motd / rcon.port / enable-rcon）
- 备份恢复会误删插件，改为默认只回滚存档（`--scope all` 才完整回滚）
- Modrinth 搜索的 `facets` 参数缺少外层数组导致 HTTP 400
- 设置读取时先调用的 setter 会把未读取的默认值写回文件，导致动画速度与自定义数据目录每次启动被重置
- 数据目录路径拼接错误（`…\McServerManager\mcsm-cli\McServerManager`）
- 页面切换动画被中断后旧页面残留，造成控件堆叠与拖影（改为位图快照淡出）
- 窗口只有左上角一小块可拖动；改为整个标题栏可拖动
- 下拉列表在深色系统主题下渲染为黑底黑字（改为显式调色板 + Fusion 样式 + 弹出层样式）
- 内容高于窗口时卡片被压缩导致文字重叠/被截断（改为按自然高度滚动）
- 纯色风格下多个控件仍绘制渐变（按钮 / 卡片 / 开关 / 分段控件 / 侧栏 / 标题栏）
- 若干编码陷阱：`QLatin1String` 处理中文导致乱码；命令行参数把全局开关的值误当作命令名
- 实验性本机 JDK 模式：服务端 stdin 关闭导致控制台刷屏、启动器继承输出句柄导致调用方挂起
