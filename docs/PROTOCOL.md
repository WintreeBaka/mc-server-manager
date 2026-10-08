# 前后端协议

后端 `mcsm-cli` 只向 **stdout** 输出机器可读内容，诊断信息走 stderr 与日志文件。

## 全局参数

| 参数 | 说明 |
| --- | --- |
| `--home <目录>` | 数据根目录（默认 `%LOCALAPPDATA%\McServerManager`） |
| `--pretty` | 输出带缩进的 JSON（默认单行紧凑） |
| `--verbose` / `--quiet` | 是否把诊断信息写到 stderr |

> 全局开关放在命令前后都可以，`--pretty versions` 不会把 `versions` 当成参数值吞掉。

## 结果信封

普通命令输出**一行** JSON：

```json
{"ok":true,"data":{...},"warnings":["..."]}
```

```json
{"ok":false,"error":{"code":"DOCKER_UNAVAILABLE","message":"Docker 未运行","detail":"..."}}
```

- `ok=false` 时 `data` 仍可能存在，用来携带上下文（例如启动失败时的 `logTail`、`hint`）
- 常见错误码：`INVALID_ARGUMENT`、`NOT_FOUND`、`ALREADY_EXISTS`、`DOCKER_UNAVAILABLE`、
  `IMAGE_PULL_FAILED`、`DOWNLOAD_FAILED`、`VERSION_RESOLVE_FAILED`、`SERVER_START_FAILED`、
  `RCON_ERROR`、`BACKUP_FAILED`、`RESTORE_FAILED`、`CONFIG_WRITE_FAILED`、`PLUGIN_SEARCH_FAILED`

## 流式输出（NDJSON）

耗时命令（`install --progress`）、日志跟随（`server logs --follow`）与守护进程（`daemon`）
会逐行输出事件，每行一个 JSON：

```json
{"type":"progress","stage":"下载服务端","percent":62,"detail":"已下载 68%"}
{"type":"log","line":"[12:00:01 INFO]: Done (12.345s)!"}
{"type":"end","code":0}
```

- `install --progress`：先若干条 `progress`，最后跟一条标准信封
- `server logs --follow`：持续输出 `log`，进程结束时输出 `end`
- `daemon`：每个检查周期输出一条**标准信封**（`data` 为本次调度报告）

界面侧由 `BackendClient` 负责拆行解析：`type=progress` 交给进度回调，带 `ok` 字段的行作为最终结果。

## 典型请求/响应

```jsonc
// mcsm-cli server start --id 生存服 --wait 120
{
  "ok": true,
  "data": {
    "status": "running",
    "message": "服务器已启动",
    "elapsedMs": 20986,
    "logTail": ["...", "[21:08:50 INFO]: Done (20.986s)! For help, type \"help\""]
  }
}
```

```jsonc
// 启动失败：同时在 message 里给出可读原因，detail 里带上原始输出
{
  "ok": false,
  "data": {
    "status": "error",
    "hint": "端口被占用，请更换端口或关闭占用该端口的程序",
    "logTail": ["Error: Invalid or corrupt jarfile server.jar"]
  },
  "error": {
    "code": "SERVER_START_FAILED",
    "message": "服务端文件损坏或不是可执行服务端，请重新下载",
    "detail": "服务端文件损坏...\n<日志末尾>"
  }
}
```

```jsonc
// mcsm-cli config apply --id 生存服 --values '{"motd":"欢迎"}'
{
  "ok": true,
  "data": {
    "changes": [{"key": "motd", "before": "A Minecraft Server", "after": "欢迎"}],
    "count": 1,
    "raw": "..."
  },
  "warnings": ["修改前已自动备份，可随时回滚"]
}
```

## 插件协议（管理器 ↔ 插件）

管理器插件（Plugin SDK）不复用上面的命令协议，而是**每次调用拉起一个独立进程**，
用「一行 JSON 进、一行 JSON 出」的方式通信（细节见 [PLUGIN-SDK.md](PLUGIN-SDK.md)）：

```jsonc
// 管理器 -> 插件（stdin，一行）
{"apiVersion":"1","plugin":"com.example.perf-tuner","method":"server.beforeStart",
 "params":{"server":{"id":"survival","memory":"16G"}},"caller":"mcsm-cli"}

// 插件 -> 管理器（stdout，一行；可先输出若干 {"type":"log"} 进度行）
{"ok":true,"data":{"memoryGb":16},
 "patch":{"jvmArgs":["-XX:+UseZGC"],"note":"已应用 ZGC"},
 "warnings":[],"log":["applied tuning"]}

// 失败
{"ok":false,"error":{"code":"METHOD_NOT_FOUND","message":"未实现该钩子","detail":""}}
```

调用方（`mcsm-cli plugin call` / `plugin hook`）会把结果包在常规信封里返回，
因此界面与脚本仍然只面对一套 JSON 约定。
