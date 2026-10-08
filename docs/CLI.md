# 后端命令参考

`mcsm-cli` 是完整的后端，界面只是它的一个调用方。所有命令输出 JSON（见 [PROTOCOL.md](PROTOCOL.md)）。

```bash
mcsm-cli help          # 打印命令列表
mcsm-cli --pretty ...  # 人类可读的缩进 JSON
```

## 环境与版本

| 命令 | 说明 |
| --- | --- |
| `doctor` | 环境自检：Docker 守护进程、已缓存的 JDK 镜像、tar/PowerShell、数据目录、服务器数量 |
| `types` | 支持的服务端类型与说明 |
| `versions --type <paper\|purpur\|vanilla\|fabric>` | 可用游戏版本列表（含推荐 JDK 与镜像名），按版本号从新到旧 |
| `version` | 后端版本、数据根目录、User-Agent |

## Java / JDK

| 命令 | 说明 |
| --- | --- |
| `java --action list` | 宿主机 Java 信息与已缓存的容器镜像 |
| `java --action ensure --major 21` | 预拉取对应 JDK 镜像 |
| `java --action scan` | 扫描本机已安装的 JDK（JAVA_HOME、PATH、各厂商安装目录），返回版本、厂商、体积、路径 |

## 安装服务器

```bash
# 自动模式：下载服务端 + 拉取 JDK 镜像 + 生成配置
mcsm-cli install --name 生存服 --type paper --version 1.21.8 \
  --port 25565 --memory 4G --level-name world \
  --gamemode survival --difficulty easy --max-players 20 \
  --eula true --progress

# 手动模式：使用自己的服务端 jar
mcsm-cli install --name 模组服 --manual true --jar D:/server.jar \
  --image eclipse-temurin:17-jre

# 实验性：使用本机 JDK（会克隆到服务器目录，直接在本机运行）
mcsm-cli install --name 测试服 --type paper --version 1.21.8 \
  --runtime host --jdk-home "C:/Java/jdk-21"
```

| 参数 | 默认 | 说明 |
| --- | --- | --- |
| `--name` | 必填 | 服务器名称 |
| `--id` | 由名称生成 | 服务器标识（目录名与容器名后缀） |
| `--type` | `paper` | `paper` / `purpur` / `vanilla` / `fabric` |
| `--version` | 必填（自动模式） | 游戏版本 |
| `--manual` | `false` | 手动模式（配合 `--jar`） |
| `--jar` | — | 本地服务端 jar 路径 |
| `--runtime` | `docker` | `docker` 或 `host`（实验性） |
| `--jdk-home` | — | 实验模式下要克隆的 JDK 目录 |
| `--image` | 按版本推导 | 覆盖容器镜像 |
| `--java` | 自动 | 覆盖 JDK 大版本 |
| `--port` / `--rcon-port` | 自动分配 | 端口 |
| `--memory` / `--memory-limit` | `4G` / 同内存 | JVM 堆与容器内存上限 |
| `--level-name` / `--motd` | `world` / 服务器名 | 初始配置 |
| `--gamemode` / `--difficulty` / `--max-players` | 默认值 | 初始配置 |
| `--eula` | `true` | 是否同意 EULA |
| `--auto-start` / `--auto-restart` | `false` / `true` | 开机自启（记录）/ 崩溃重启（容器策略） |
| `--overrides '{"key":"value"}'` | — | 直接写入 server.properties 的额外键值 |
| `--progress` | — | 输出 NDJSON 进度事件 |
| `--skip-docker` | `false` | 跳过 Docker 检查（只生成文件） |

## 服务器管理

| 命令 | 说明 |
| --- | --- |
| `server list` | 列出所有服务器（含状态、端口、内存、备份计划） |
| `server get --id <id>` | 单台服务器详情 + 最近日志 |
| `server status --id <id>` | 状态与资源占用（容器 CPU / 内存） |
| `server start --id <id> [--wait 120]` | 启动并等待就绪；失败时返回可读原因与日志末尾 |
| `server stop --id <id> [--force]` | 先 `save-all flush` 再优雅停止（`--force` 直接结束） |
| `server restart --id <id>` | 停止后重新启动 |
| `server kill --id <id>` | 强制停止 |
| `server logs --id <id> [--tail 200] [--follow]` | 读取日志；`--follow` 输出 NDJSON 日志流 |
| `server command --id <id> --command "say hi"` | 通过 RCON 发送指令 |
| `server save --id <id>` | 执行 `save-all flush` |
| `server update --id <id> [参数]` | 修改记录：`--name` `--memory` `--image` `--java` `--port` `--eula` `--motd` `--level-name` `--auto-restart` `--java-options` |
| `server delete --id <id> [--purge true]` | 删除记录（`--purge` 连目录一起删除） |

## 配置文件

| 命令 | 说明 |
| --- | --- |
| `config schema` | 35 项配置的元数据（分组、类型、范围、说明、是否进阶项） |
| `config read --id <id>` | 读取键值 + 原文 |
| `config apply --id <id> --values '{"motd":"欢迎"}'` 或 `--values-file values.json` | 写入指定键（保留注释），写入前自动备份 |
| `config raw --id <id> --text "..."` 或 `--file <路径>` | 专家模式：整文件替换（同样先备份） |
| `config validate --id <id> [--text "..."]` | 语法与取值检查（重复键、类型、范围） |
| `config backups --id <id>` | 配置备份历史（含改动明细与内容预览） |
| `config rollback --id <id> --backup <备份ID>` | 回滚到某个历史版本 |

## 备份

| 命令 | 说明 |
| --- | --- |
| `backup list --id <id>` | 备份列表（大小、时间、来源、备注）+ 计划与总占用 |
| `backup create --id <id> [--note "..."] [--plugins true] [--save-first true]` | 立即备份；运行中的服务器会先存档 |
| `backup restore --id <id> --backup <文件名> [--restart true] [--scope world\|all]` | 恢复存档（默认只回滚世界并保留插件/配置） |
| `backup remove --id <id> --backup <文件名>` | 删除某个备份 |
| `backup schedule --id <id> --enabled true --interval 180 --keep 10 [--plugins true] [--save-first true]` | 定时备份计划 |

## 服务器插件（装进某台服务器的 jar）

| 命令 | 说明 |
| --- | --- |
| `plugin sources` | 可用插件源 |
| `plugin search --id <id> --source modrinth --query EssentialsX [--limit 25]` | 搜索（自动按服务器类型与版本过滤） |
| `plugin install --id <id> --source modrinth --slug essentialsx` | 安装（解析与当前版本兼容的最新构建） |
| `plugin list --id <id>` | 已安装插件（含启用状态） |
| `plugin toggle --id <id> --file X.jar --enabled false` | 启用 / 禁用（禁用即重命名为 `.jar.disabled`） |
| `plugin remove --id <id> --file X.jar` | 删除插件 |

## 管理器插件包（Plugin SDK）

扩展管理器本身（前端页面 / 后端钩子 / Web 面板），安装位置是 `<数据根>/plugins/`，
完整接口见 [PLUGIN-SDK.md](PLUGIN-SDK.md)。

| 命令 | 说明 |
| --- | --- |
| `plugin api` | 导出机器可读的接口清单（作用域、前端命名空间、钩子、CLI 命令） |
| `plugin packages [--enabled]` | 已安装的插件包（含作用域、启用状态、来源 URL） |
| `plugin package inspect --zip <file>` / `--url <url>` | 只校验：解析 `plugin.json`、自动识别作用域、检查入口文件，不写入磁盘 |
| `plugin package install --zip <file> [--force] [--disabled] [--expect <id>]` | 从本地 zip 安装（`--force` 覆盖同名插件） |
| `plugin package install --url <url> [--force]` | 下载并安装（解压使用随包内置的 7-Zip） |
| `plugin package list` | 同 `plugin packages` |
| `plugin package info --name <id>` | 详情：清单、启动命令、Web 服务状态 |
| `plugin package enable` / `disable --name <id>` | 启用 / 停用（触发 `plugin.enable` / `plugin.disable` 钩子） |
| `plugin package remove --name <id> [--purge-data]` | 卸载（触发 `plugin.uninstall`；`--purge-data` 同时删除插件数据） |
| `plugin call --name <id> --method <m> [--params '{...}'] [--timeout ms]` | 调用插件后端方法（JSON-Line RPC） |
| `plugin hook --name <id>` / `--all --hook <h> [--payload '{...}']` | 手动触发钩子（`--all` 广播给所有声明了该钩子的插件） |
| `plugin service start` / `stop` / `status --name <id>` | 管理插件自带的本地 Web 面板服务 |

```bash
# 从示例插件包安装并试跑
mcsm-cli plugin package inspect --zip dist/plugins/com.example.ops-console-1.0.0.zip
mcsm-cli plugin package install --zip dist/plugins/com.example.ops-console-1.0.0.zip
mcsm-cli plugin call --name com.example.ops-console --method summary
mcsm-cli plugin service start --name com.example.ops-console
```

## 调度与设置

| 命令 | 说明 |
| --- | --- |
| `schedule tick` | 执行一次调度检查（到了间隔就自动备份，并清理超出的自动备份） |
| `schedule status` | 每台服务器的计划状态与下次到期时间 |
| `daemon [--interval 60] [--once]` | 常驻守护进程（界面启动时会自动拉起；`--once` 只跑一次） |
| `settings get` / `settings set --theme dark --animations true` | 读写后端设置 |
