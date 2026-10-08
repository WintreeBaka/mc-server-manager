# 架构说明

## 总体结构

项目由两个可执行文件组成，它们是**两个独立进程**，只通过标准输入输出上的 JSON 通信：

| 目标 | 语言/框架 | 职责 |
| --- | --- | --- |
| `McServerManager` | Qt 6 Widgets | 界面、主题、动画、状态展示、调用后端 |
| `mcsm-cli` | Qt 6 Core + Network | Docker 编排、下载、配置读写、备份、插件、调度 |

这样做的原因：界面重构不会碰到业务逻辑；后端可以单独用于脚本、CI 或未来的 Web 面板。

```
frontend/
  app/      主题(ThemeManager) · 缓动曲线(Easing) · 后端通信(BackendClient) · 全局上下文(AppContext)
  model/    ServerInfo / ServerModel（列表模型与状态枚举）
  plugin/   插件宿主(PluginHost)：QJSEngine 执行插件前端脚本，桥接 mcsm.* 接口
  ui/       主窗口 · 标题栏 · 导航 · Toast · 无边框窗口助手
  widgets/  渐变按钮 · 卡片 · 开关 · 分段控件 · 动画栈 · 日志视图 · 行内标签
  pages/    总览 · 服务器 · 控制台 · 配置文件 · 备份 · 服务器插件 · 设置 · 新建向导
            + 插件扩展页 / 插件导入对话框 / 插件页面渲染器

backend/
  cli/      参数解析(ArgParser) 与命令路由(CommandRouter)
  core/     路径(AppPaths) · 日志(Logger) · 进程(ProcessRunner) · JSON(JsonUtil) · 字符串工具
  net/      HTTP 客户端(HttpClient) · 版本解析(VersionResolver：Mojang/Paper/Purpur/Fabric)
  docker/   容器生命周期(DockerManager) · 生成文件模板(TemplateWriter)
  java/     JDK 镜像管理与本机 JDK 扫描(JavaManager)
  server/   记录(ServerRecord/ServerStore) · 安装器(ServerInstaller) · 生命周期(ServerLifecycle) · RCON
  config/   配置字段表(ConfigSchema) · 读写与备份回滚(ConfigManager)
  backup/   存档打包、恢复、保留策略(BackupService)
  plugin/   服务器插件市场(PluginManager) · 管理器插件包(PackageManager) · 插件运行时(PluginRuntime)
  schedule/ 定时备份守护进程(Scheduler)
```

## 插件系统（Plugin SDK）

管理器插件包由三个模块组成，边界很清晰：

| 模块 | 职责 |
| --- | --- |
| `backend/src/core/Archive.cpp` | 用随包内置的 7-Zip 解压插件包（找不到内置副本时回退系统 7z / tar） |
| `backend/src/plugin/PackageManager.cpp` | 安装 / 卸载 / 启停、`registry.json`、`plugin.json` 校验、**作用域自动识别**、`plugin api` 接口清单 |
| `backend/src/plugin/PluginRuntime.cpp` | 后端插件的 JSON-Line 进程调用、生命周期钩子分发、启动补丁、Web 面板服务 |
| `frontend/src/plugin/PluginHost.cpp` | QJSEngine 宿主 + `mcsm.*` API 桥（页面注册、Toast、存储、调用后端、事件订阅） |

两个关键约束：

1. **前端脚本没有原生能力**：只能调用 `mcsm.*` 暴露的接口，拿不到文件系统或进程；脚本异常只影响该插件。
2. **后端插件是独立进程**：请求一行 JSON、响应一行 JSON，超时即终止；钩子失败只会变成 `warnings`，
   绝不会中断服务器管理流程。

性能优化类插件的主入口是 `server.beforeStart` 钩子返回的 **patch**：
`jvmArgs` / `env` / `dockerArgs` 会被合并进 `JAVA_OPTS`、容器环境变量与 `docker run` 参数，
同时写入生成的启动脚本（本机 JDK 模式），并落盘到 `<服务器目录>/.mcsm/start-patch.json` 便于排查。
补丁每次启动前重建，停用插件后自动清除。

## 关键设计

### 一个服务器 = 一个容器

```
docker run -d --name mcsm-<id> \
  --restart unless-stopped \
  -p <port>:<port>/tcp \
  -p 127.0.0.1:<rcon>:<rcon>/tcp \
  -v <数据目录>/servers/<id>:/data \
  -w /data -e MEMORY=4G -e JAVA_OPTS=... \
  eclipse-temurin:21-jre /bin/sh /data/start.sh
```

- 端口只按记录映射游戏端口；**RCON 端口只绑定到本机回环地址**，供管理器发指令，不暴露到局域网
- 服务器目录直接 bind mount 到 `/data`，因此备份、配置、世界文件在宿主机上可见，容器只是 JDK 运行环境
- JDK 版本按游戏版本推导（`VersionResolver::recommendedJavaMajor`），原版还会直接读取 Mojang 元数据里声明的 JDK

### 控制台通道：日志用 docker logs，指令用 RCON

不用 `docker attach`（会抢走标准输入）。日志通过 `docker logs -f` 转成 NDJSON 流给界面；
发送指令走自行实现的精简 RCON 客户端（`server/RconClient`）。

RCON 报文格式为小端序：`长度(4) | 请求ID(4) | 类型(4) | 正文 | \0\0`，其中长度字段**已经包含** ID、类型与结尾两个空字节。

### 配置写入保留原文

`ConfigManager::applyValues` 逐行解析 `server.properties`，只替换目标键，注释、空行、未知键原样保留；
写入前把旧文件快照进 `config-backups/`，并在 `index.json` 里记录本次改动的 key / 原值 / 新值，
因此回滚是精确的，历史也是可读的。差异统计只针对**本次提交的键**，不会把未提交的键算成“被改成空”。

### 备份

1. 运行中的服务器先通过 RCON `save-off` + `save-all flush`，避免打包到写入中的区块
2. 优先在容器内 `tar czf`（宿主机目录已挂载，归档直接落盘）；容器不可用时回退到宿主机 `tar`
3. 打包结束再 `save-on`；归档与元数据写入 `backups/index.json`
4. 保留策略只清理**自动备份**，手动备份不会被自动删除
5. 恢复默认只回滚世界存档并保留插件与配置（`--scope all` 才是完整回滚），恢复前会自动再打一份快照

### 界面动画

- `Easing` 用三次贝塞尔控制点生成曲线（例如 `(0.22, 1, 0.36, 1)`），所有动画共用
- 页面切换时**只有一个真实页面可见**：旧页面先被 `grab()` 成位图再淡出，新页面同时滑入。
  这样即使连续快速点击菜单也不会出现页面堆叠或拖影（早期版本依赖动画完成信号隐藏旧页面，被中断后就留下了残影）
- 动画速度系数会同时作用于页面切换、开关、Toast、列表与滚动条

### 原生控件与主题

Qt 的样式表无法覆盖所有的地方（下拉列表弹出层、菜单、工具提示在 Windows 上会跟随**系统**主题），
因此除了样式表，程序还会：

1. 显式设置与主题一致的 `QPalette`
2. 使用 **Fusion** 样式，让控件完全由 Qt 自绘（原生文件对话框仍然是系统的）
3. 每次切换主题时，额外为每个下拉框的弹出列表设置样式

### 实验性：本机 JDK

`runtime = host` 的服务器不使用容器：安装时把所选 JDK 完整克隆到 `<服务器目录>/jdk`，
启动时直接执行该 JDK 的 `java`（Windows 下用 `cmd /c start.cmd`，脚本里用一条常驻 `ping`
保持控制台 stdin 打开，否则 Minecraft 会因为 stdin 关闭而疯狂打印 `>` 把日志撑爆）。
进程存活通过 pid 文件 + RCON 端口探测双重判断，停止时先 RCON `stop`，超时再结束进程树与残留的 java 进程。

## 数据流示例：启动一台服务器

```
界面点击「一键启动」
  → BackendClient 执行 `mcsm-cli server start --id <id> --wait 120`
    → ServerLifecycle::start
       · 校验目录 / jar / EULA
       · 重建 start.sh、eula.txt、docker-compose.yml
       · 确认镜像存在（不存在则 docker pull）
       · docker run 启动容器
       · 循环读取 docker logs，等待 "Done (" 或识别错误特征
    → 返回 JSON：{ok:true, data:{status:"running", logTail:[...]}}
  → 界面更新状态点、按钮、日志预览；失败时弹出错误横幅并展示日志末尾
```

## 目录约定

| 路径 | 说明 |
| --- | --- |
| `<数据根>/servers.json` | 所有服务器记录（端口、内存、JDK、备份计划…） |
| `<数据根>/gui.ini` | 界面设置（主题、色彩风格、字体、后端路径、数据目录） |
| `<数据根>/logs/backend.log` | 后端诊断日志（启动、下载、docker 命令、错误） |
| `<数据根>/plugins/` | 管理器插件包（`registry.json` + 每个插件一个目录 + `.data/<id>/` 私有数据） |
| `<数据根>/servers/<id>/` | 单个服务器目录，容器内挂载为 `/data` |
