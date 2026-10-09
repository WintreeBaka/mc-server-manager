# McServerManager 插件接口说明（Plugin SDK）

版本：**apiVersion 1** · 适用于 McServerManager 1.1.1 及以上

本文档描述 McServerManager 的**插件包（plugin package）**机制：如何打包、导入、启用，以及
前端（桌面界面）、后端（CLI 能力）、支持库 / Web 面板三类扩展分别可以调用哪些接口。

> 注意区分两个概念：
>
> * **Minecraft 插件**（Bukkit/Paper 的 `.jar`）：由「插件市场」页面管理，装进服务器的 `plugins/` 目录。
> * **管理器插件包**（本文档）：扩展 McServerManager 本身，给管理器加页面、加后端能力、加运维面板。
>
> 两者的命令都在 `mcsm-cli plugin` 下：前者是 `plugin search/install/list ...`（需要 `--id <服务器>`），
> 后者是 `plugin package ...`（全局，不需要服务器）。

---

## 1. 总览

```
                +----------------------- 插件包 (zip) -----------------------+
                |  plugin.json   frontend/    backend/    web/    README.md |
                +------------------------------------------------------------+
                                     |
   导入（本地 zip / URL） -> mcsm-cli plugin package install -> <数据目录>\plugins\<id>\
                                     |
                +--------------------+--------------------+
                v                                         v
     前端：QJSEngine 执行 frontend/index.js    后端：独立进程 + JSON-Line 协议
     （mcsm.* API -> 页面 / 表格 / 通知 / 调用） （钩子 + RPC -> 调优 / 服务 / 数据）
                |                                         |
                +---------- mcsm-cli 命令 / 事件 ----------+
```

| 作用域 | 含义 | 典型用途 |
| --- | --- | --- |
| `frontend` | 只扩展桌面界面 | 自定义面板、服务器看板、可视化统计 |
| `backend` | 只扩展后端能力 | JVM 调优、自定义生命周期钩子、数据采集 |
| `web` | 支持库 / Web 面板 | 自带本地 HTTP 服务，用浏览器访问的运维台 |
| `global` | 同时包含前端与后端 | 前端页面 + 后端数据接口的完整工具 |

作用域**不需要手工声明**：导入时管理器会扫描包内目录自动识别，也可以显式写在 `plugin.json` 里。

---

## 2. 快速开始

```powershell
# 1) 打包：把插件目录压成 zip（也可以用任意压缩工具，只要 zip 里能读到 plugin.json）
powershell -File examples\plugins\build-examples.ps1

# 2) 预览：只校验不安装，输出识别到的作用域
build\bin\mcsm-cli.exe plugin package inspect --zip dist\plugins\com.example.hello-panel-1.0.0.zip

# 3) 安装（--force 覆盖同名插件，--disabled 安装后先不启用）
build\bin\mcsm-cli.exe plugin package install --zip dist\plugins\com.example.hello-panel-1.0.0.zip
build\bin\mcsm-cli.exe plugin package install --url https://example.com/my-plugin-1.0.0.zip --force

# 4) 查看 / 启停 / 卸载
build\bin\mcsm-cli.exe plugin packages
build\bin\mcsm-cli.exe plugin package info --name com.example.hello-panel
build\bin\mcsm-cli.exe plugin package disable --name com.example.hello-panel
build\bin\mcsm-cli.exe plugin package remove  --name com.example.hello-panel

# 5) 调用插件的后端方法 / 手动触发钩子
build\bin\mcsm-cli.exe plugin call --name com.example.ops-console --method summary
build\bin\mcsm-cli.exe plugin hook --all --hook server.afterStart --payload "{\"server\":{\"id\":\"demo\"}}"
```

图形界面里的等价入口：**设置 -> 插件扩展 -> 添加插件**（本地 zip / URL 两个来源）。
安装后左侧菜单会自动出现插件注册的页面。

---

## 3. 插件包结构

```
my-plugin-1.0.0.zip
+-- plugin.json            必需，插件清单（见第 4 节）
+-- README.md              建议提供
+-- frontend/              前端部分（可选）
|   +-- index.js           入口脚本，由 QJSEngine 执行
+-- backend/               后端部分（可选）
|   +-- index.js           入口脚本（node / python / exe / 任意可执行）
+-- web/                   支持库 / Web 面板静态资源（可选）
    +-- index.html
```

* 包内允许有一层顶层目录（例如资源管理器“压缩文件夹”产生的 `my-plugin/plugin.json`），
  导入时管理器会自动向内查找 `plugin.json`。
* 只支持 **zip**。解压使用随程序发布的内置 7-Zip（`third_party/7zip/7za.exe`），
  目标机器**不需要**安装任何压缩软件；找不到内置副本时会回退到系统 7-Zip。
* 文件使用 UTF-8 编码；JSON 不要带 BOM。

---

## 4. `plugin.json` 完整字段

```json
{
  "id": "com.example.ops-console",
  "name": "运维面板",
  "version": "1.0.0",
  "apiVersion": 1,
  "minManagerVersion": "1.1.1",
  "scope": "global",
  "description": "一句话说明这个插件做什么",
  "author": "你的名字",
  "homepage": "https://github.com/you/my-plugin",
  "license": "MIT",
  "permissions": ["ui.pages", "servers.read", "network.listen"],

  "frontend": { "entry": "frontend/index.js" },
  "backend": {
    "entry": "backend/index.js",
    "runtime": "node",
    "args": [],
    "timeoutMs": 20000
  },
  "web": {
    "entry": "web/index.html",
    "service": "backend/server.js",
    "port": 18765,
    "title": "运维面板"
  },
  "hooks": ["server.beforeStart", "server.afterStart"],
  "contributes": {
    "pages": [
      { "id": "ops", "title": "运维面板", "icon": "op", "subtitle": "服务器总览" }
    ]
  }
}
```

| 字段 | 必需 | 说明 |
| --- | --- | --- |
| `id` | 是 | 插件唯一标识，建议反向域名（`com.example.my-plugin`）。只允许 `A-Za-z0-9._-`，用作安装目录名 |
| `name` | 是 | 显示名称（支持中文） |
| `version` | 是 | 插件版本，如 `1.0.0` |
| `apiVersion` | | **整数**接口版本（见 §4.3），必须落在管理器支持的区间内，否则拒绝加载 |
| `minManagerVersion` | | 建议使用的管理器版本，**只提示用户，不参与硬校验** |
| `scope` | | `frontend` / `backend` / `web` / `global` / `auto`（默认 `auto`，即自动识别） |
| `description` / `author` / `homepage` / `license` | | 元信息，显示在插件列表里 |
| `permissions` | | 插件声明的权限（见第 8 节），用于提示用户，不做强制拦截 |
| `frontend.entry` | | 前端入口脚本（相对插件根目录）。缺省时自动寻找 `frontend/index.js` |
| `backend.entry` | | 后端入口（脚本或可执行文件）。缺省时自动寻找 `backend/index.js`、`backend/main.py` 等 |
| `backend.runtime` | | `auto`（默认，按扩展名推断）/ `node` / `python` / `java` / `exec` |
| `backend.args` | | 追加到入口命令后的参数数组 |
| `backend.timeoutMs` | | 单次调用超时，默认 30000 |
| `web.entry` | | Web 面板静态页面（相对路径），缺省时自动寻找 `web/index.html` |
| `web.service` | | 需要常驻的本地服务入口（`plugin service start` 启动它） |
| `web.port` | | 服务端口，会作为环境变量 `MCSM_PLUGIN_PORT` 传入 |
| `web.title` | | 面板标题 |
| `hooks` | | 后端要接收的钩子名，支持 `server.*` 这类通配（见第 6.3 节） |
| `contributes.pages` | | 供界面预展示的页面清单（前端脚本仍应调用 `mcsm.ui.registerPage`） |

最小可用清单只需要 `id`、`name`、`version`，再加上至少一个入口。

### 4.1 作用域自动识别规则

1. 若显式写了 `scope`，以它为准（并校验对应入口是否存在）。
2. 否则按目录结构推断：

| 检测到的部分 | 识别结果 |
| --- | --- |
| 只有 `frontend/`（或 `frontend.entry`） | `frontend`（前端扩展） |
| 只有 `backend/` | `backend`（后端服务） |
| 只有 `web/` | `web`（支持库 / Web 面板） |
| `frontend/` + `backend/` | `global`（全局） |
| 任意两项以上 | `global` |

3. 识别不出内容、入口文件不存在、或 `apiVersion` 超出支持区间：**拒绝导入**并给出具体原因。

### 4.3 apiVersion：整数区间

`apiVersion` 是**整数**，不是语义化版本：管理器每增加**一批新接口**就 `+1`。

| 常量 | 含义 |
| --- | --- |
| `SUPPORTED_MIN_API` | 仍然支持的最老接口级别。**只有破坏性变更时**才抬高——抬高的同时旧插件自然淘汰 |
| `SUPPORTED_MAX_API` | 当前级别，等于管理器的 `apiVersion` |

* 插件的 `apiVersion` **不在 `[MIN, MAX]` 区间内 → 拒绝加载**，日志与界面都会写明：
  `插件 apiVersion 2 不受支持：本管理器支持 1 - 1`。
* 兼容写法：`apiVersion: 1`（推荐整数）。写成字符串 `"1"` 仍可加载，但会给出"请写成整数"的提示。
* 省略 `apiVersion` 时按当前级别处理（`SUPPORTED_MAX_API`）。
* `minManagerVersion` 只是给用户看的提示：管理器版本更低时给出警告，**不会**阻止插件加载。

```json
{
  "name": "my-plugin",
  "version": "1.0.0",
  "apiVersion": 1,
  "minManagerVersion": "1.1.1"
}
```

当前级别可以用 `mcsm-cli plugin api` 查询：

```json
{
  "apiVersion": 1,
  "supportedMinApi": 1,
  "supportedMaxApi": 1,
  "managerVersion": "1.1.1",
  "apiVersionPolicy": {
    "type": "integer",
    "bumpOn": "每增加一批新接口 +1",
    "rejectWhen": "不在 supportedMinApi..supportedMaxApi 区间内",
    "minManagerVersion": "仅提示用户，不参与硬校验"
  }
}
```

> 导入对话框会先调用 `plugin package inspect`，并把识别结果（例如“识别为：全局（前端 + 后端）”）
> 显示给用户，确认后才会真正写入磁盘。

### 4.2 安装位置与数据目录

| 路径 | 说明 |
| --- | --- |
| `<数据目录>/plugins/` | 插件安装根目录（Windows 默认 `%LOCALAPPDATA%\McServerManager\plugins`） |
| `<数据目录>/plugins/<id>/` | 单个插件解压后的全部内容（含 `install.json` 安装记录） |
| `<数据目录>/plugins/registry.json` | 注册表：id、版本、作用域、启用状态、来源 URL、安装时间 |
| `<数据目录>/plugins/.data/<id>/` | 插件私有数据（卸载默认保留，加 `--purge-data` 才删除） |
| `<服务器目录>/.mcsm/start-patch.json` | 后端插件对本次启动生效的参数补丁（见第 6.4 节） |

---

## 5. 前端插件 API

前端脚本在 **QJSEngine**（ES2020，无 DOM、无 `require`）中执行，入口脚本运行时会拿到全局对象 `mcsm`。
脚本出错不会影响管理器本身，错误会显示在「设置 -> 插件扩展」页面的状态行。

### 5.1 全局对象

| 属性 | 说明 |
| --- | --- |
| `mcsm.version` | 管理器版本，如 `1.1.1` |
| `mcsm.apiVersion` | 当前接口级别（整数，如 `1`） |
| `mcsm.plugin.id / .name / .version / .scope / .path` | 当前插件信息 |

### 5.2 日志与通知

```js
mcsm.log("写入管理器日志");
mcsm.toast("存档完成", "success");     // level: info | success | warn | error
mcsm.openUrl("https://example.com");   // 用系统浏览器打开
mcsm.utils.humanBytes(1536);           // -> "1.5 KB"
```

### 5.3 界面扩展（UI 扩展类插件）

```js
var pageId = mcsm.ui.registerPage({
  id: "my-page",          // 省略时自动生成
  title: "我的页面",
  icon: "S",              // 侧边栏图标（任意字符或 emoji）
  subtitle: "一句话说明"
});

mcsm.ui.setPageContent(pageId, [ /* 区块数组，见下表 */ ]);
mcsm.ui.openPage(pageId);   // 让管理器跳转到该页面
```

注册成功后：左侧主菜单会出现一个新条目（点击即可切换，沿用原有的动画过渡），
页面内容完全由 `setPageContent` 决定，主题、字体、配色会自动跟随用户设置（暗黑/亮色、渐变/纯色）。

**区块类型**（`setPageContent` 数组的元素）：

| type | 字段 | 效果 |
| --- | --- | --- |
| `heading` | `text` | 标题 |
| `text` | `text`, `style`(`hint` / `caption` / `subtitle`) | 段落文本 |
| `keyvalue` | `items: [["键","值"], ...]` 或 `[{key,value}]` | 键值表 |
| `table` | `title`, `columns: []`, `rows: [[...]]` | 表格 |
| `buttons` | `items: [{label, icon, command, url, openUrlFrom, toast, level}]` | 按钮行 |
| `log` | `title`, `text` | 控制台风格输出框（按钮执行结果也会写进这里） |
| `html` | `html` | 富文本片段（Qt 富文本子集） |
| `divider` | 无 | 分隔线 |
| `progress` | `label`, `value`(0-100) | 进度条 |

`buttons` 的三种行为：

1. `command`：执行一条 mcsm-cli 命令（例如 `"server list"`、`"plugin call --name x --method y"`），
   结果写入同页面的 `log` 区块；
2. `url`：直接用浏览器打开；
3. `openUrlFrom: "url"`：先执行 `command`，再从返回数据里取该字段作为 URL 打开
   （例如 `plugin service start` 返回的 `url`）。

### 5.4 服务器数据

```js
var servers = mcsm.servers.list();   // 缓存快照，立即返回，不会阻塞界面
var current = mcsm.servers.selected();
mcsm.refreshServers();               // 需要最新数据时再刷新（异步，刷新后触发 servers.refreshed 事件）
```

> `mcsm.servers.list()` **不会**去调用后端：它读取管理器已有的缓存，
> 因此即使后端很慢也不会卡住界面（旧版本会同步启动 `mcsm-cli`，一个插件就能让窗口卡住）。

### 5.5 调用后端

```js
mcsm.backend.invoke("server list", function (reply) {
  // reply = { ok, data, warnings, error, code }
  if (reply.ok) mcsm.toast("共 " + reply.data.count + " 台服务器", "info");
});

// 也可以传参数数组，避免转义
mcsm.backend.invoke(["plugin", "call", "--name", "com.example.ops-console", "--method", "summary"],
                    function (reply) { mcsm.log(JSON.stringify(reply.data)); });
```

所有 mcsm-cli 命令都可以这样调用，回调里的 `data` 就是命令输出的 `data` 字段。

### 5.6 事件订阅

```js
mcsm.events.on("servers.refreshed",   function (payload) { /* payload.count */ });
mcsm.events.on("server.selected",     function (payload) { /* payload.serverId */ });
mcsm.events.on("theme.changed",       function (payload) { /* payload.dark, payload.style */ });
mcsm.events.on("environment.changed", function (payload) { /* doctor 数据 */ });
mcsm.events.on("app.ready",           function (payload) { /* payload.pluginCount */ });
```

| 事件 | 触发时机 | payload |
| --- | --- | --- |
| `app.ready` | 所有前端插件加载完成后 | `{pluginCount}` |
| `servers.refreshed` | 服务器列表刷新成功 | `{count}` |
| `server.selected` | 用户切换当前服务器 | `{serverId}` |
| `environment.changed` | 环境自检（doctor）刷新 | doctor 结果 |
| `theme.changed` | 主题 / 配色切换 | `{dark, style}` |

### 5.7 数据存储

```js
mcsm.storage.set("lastRun", Date.now());   // 插件私有（按插件 id 隔离）
mcsm.storage.get("lastRun");
mcsm.storage.keys();
mcsm.storage.remove("lastRun");

mcsm.settings.get("ui/theme");             // 读取管理器设置
mcsm.settings.set("myPlugin/flag", true);
```

### 5.8 多文件

前端没有模块系统，但可以用同步 `include` 拆分脚本：

```js
mcsm.include("frontend/util.js");   // 在同一个引擎里求值，可共享全局变量
```

### 5.9 完整前端示例

见 `examples/plugins/hello-panel/`（注册页面 + 表格 + 按钮 + 事件订阅）与
`examples/plugins/ops-console/frontend/index.js`（调用自己的后端方法 + 打开 Web 面板）。

---

## 6. 后端插件 API

### 6.1 运行时

后端插件由管理器**按需拉起独立进程**，通过 **JSON-Line 协议**通信：

1. 管理器把**一行 JSON 请求**写入插件进程的 stdin；
2. 插件把**一行 JSON 结果**写到 stdout（也可以先输出若干进度行）；
3. 进程退出，管理器读取结果。

因此插件与管理器彻底隔离：插件崩溃、死循环、缺少依赖都不会拖垮管理器，超时会被强制结束。

| `backend.runtime` | 说明 |
| --- | --- |
| `node` | 需要 PATH 里有 `node`，推荐用于 JS |
| `python` | 需要 PATH 里有 `python` / `python3` |
| `java` | 以 `java -jar <entry>` 运行 |
| `exec` | 直接执行入口文件（`.exe` / `.cmd` / `.bat` / `.ps1`） |
| `auto`（默认） | 按入口扩展名推断，无法判断时按 `exec` |

缺少运行时不会被静默忽略：`plugin package info` 会显示 `launchError`，界面也会提示。

### 6.2 请求与响应格式

**请求**（管理器 -> 插件，一行 JSON）：

```json
{
  "apiVersion": 1,
  "plugin": "com.example.perf-tuner",
  "method": "server.beforeStart",
  "params": { "server": { "id": "survival", "memory": "16G" } },
  "caller": "mcsm-cli",
  "pluginsDir": "C:/Users/me/AppData/Local/McServerManager/plugins",
  "dataDir": "C:/Users/me/AppData/Local/McServerManager/plugins/.data/com.example.perf-tuner",
  "requestedAt": "2026-10-08T20:58:14"
}
```

**成功响应**：

```json
{
  "ok": true,
  "data": { "memoryGb": 16 },
  "patch": { "jvmArgs": ["-XX:+UseZGC"], "note": "已应用 ZGC" },
  "warnings": ["可选提示"],
  "log": ["可选日志行"]
}
```

**失败响应**：

```json
{ "ok": false, "error": { "code": "METHOD_NOT_FOUND", "message": "未实现该钩子", "detail": "" } }
```

**进度行**（可选，可在结果之前输出任意多行）：

```json
{ "type": "log", "line": "正在收集指标" }
```

约定：

* 一行一个 JSON 对象，UTF-8 编码，一行内结束（不要美化换行）。
* 只有带 `ok` 字段的行被当作最终结果，出现多行时最后一行生效。
* 退出码非 0 且没有合法结果行时，管理器记录 `PLUGIN_PROTOCOL_ERROR` 并展示 stderr。
* 超时（`backend.timeoutMs`，默认 30 秒）会得到 `PLUGIN_TIMEOUT`，进程被终止。

### 6.3 钩子清单

插件在 `plugin.json` 的 `hooks` 里声明要接收哪些钩子；未声明的不会被调用（避免无谓地拉起进程）。

| 钩子 | 触发时机 | `params` 主要内容 | 返回值 |
| --- | --- | --- | --- |
| `plugin.install` | 插件包安装完成后 | `plugin`、`source` | `data` |
| `plugin.enable` / `plugin.disable` | 启用 / 停用前后 | `plugin` | `data` |
| `plugin.uninstall` | 卸载前（插件目录仍存在） | `plugin`、`purgeData` | `data` |
| `server.beforeStart` | 每次启动服务器前 | `server`（完整记录）、`serverDir` | **`patch`**，或用 `cancel` + `reason` 阻止启动 |
| `server.afterStart` | 启动流程结束（成功或失败） | `server`、`ok`、`status`、`message`、`elapsedMs` | `data` |
| `server.beforeStop` / `server.afterStop` | 停止服务器前后 | `server` | `data` |
| `server.beforeBackup` / `server.afterBackup` | 备份前后 | `server`、`backup` | `data` |
| `server.configApplied` | 配置文件被修改后 | `server`、`changes` | `data` |
| `scheduler.tick` | 定时任务心跳 | `timestamp` | `data` |

通配写法：`"server.*"` 匹配所有 `server.` 前缀的钩子。

### 6.4 启动补丁（性能优化类插件的主入口）

`server.beforeStart` 的返回值可以带 `patch`。管理器把所有插件的补丁合并后写入
`<服务器目录>/.mcsm/start-patch.json`，并在真正启动时应用：

| patch 键 | 类型 | 作用 |
| --- | --- | --- |
| `jvmArgs` | `string[]` | 追加 JVM 参数。容器模式合并进 `JAVA_OPTS`；本机 JDK 模式写入生成的启动脚本 |
| `env` | `object` | 追加容器环境变量（仅容器模式） |
| `dockerArgs` | `string[]` | 追加 `docker run` 参数，例如 `["--cpus","4"]`（仅容器模式） |
| `note` | `string` | 说明，写入补丁文件并显示在启动日志中 |
| `cancel` / `reason` | `bool` / `string` | 设为 `true` 可直接阻止本次启动并给出原因 |

行为细节：

* 补丁在**每次启动前重建**；没有任何插件声明 `server.beforeStart` 时补丁文件会被删除。
* 启动脚本里会打印 `[mcsm] plugin tuning: <插件 id>`，便于确认哪个插件生效。
* 生成的启动脚本从不被手工修改，停用插件即可恢复原样。

### 6.5 自定义方法（RPC）

除了钩子，插件可以实现任意方法名，供命令行或界面调用：

```powershell
mcsm-cli plugin call --name com.example.ops-console --method summary
mcsm-cli plugin call --name com.example.ops-console --method reload --params "{\"force\":true}" --timeout 60000
```

前端脚本里的等价写法：

```js
mcsm.backend.invoke("plugin call --name com.example.ops-console --method summary", cb);
```

建议实现的元方法：

| 方法 | 约定 |
| --- | --- |
| `info` / `describe` | 返回 `{name, version, capabilities[]}`，供运维脚本自检 |
| `health` | 返回 `{ok:true}`，用于监控 |
| `shutdown` | 释放资源（停用插件前管理器会调用 `plugin.disable` 钩子） |

### 6.6 Node.js 最小示例

```js
const readline = require("readline");
const reply = (object) => process.stdout.write(JSON.stringify(object) + "\n");

readline.createInterface({ input: process.stdin }).on("line", (line) => {
  const request = JSON.parse(line);
  if (request.method === "server.beforeStart") {
    return reply({
      ok: true,
      patch: { jvmArgs: ["-XX:+AlwaysPreTouch"], note: "已应用预设参数" }
    });
  }
  reply({ ok: false, error: { code: "METHOD_NOT_FOUND", message: request.method } });
});
```

Python 版同理：读 `sys.stdin` 一行、解析 JSON、`print(json.dumps(result), flush=True)`。

---

## 7. 支持库 / Web 面板类插件

“以 Web 形式实现的运维管理”这类插件把界面放在浏览器里，管理器只负责**启动 / 停止服务**与**打开面板**。

```json
"web": { "entry": "web/index.html", "service": "backend/server.js", "port": 18765, "title": "运维面板" }
```

| 命令 | 行为 |
| --- | --- |
| `plugin service start --name <id>` | 以分离进程启动 `web.service`，返回 pid 与访问 URL |
| `plugin service status --name <id>` | 返回 `running` / `listening` / pid / URL / 静态页面路径 |
| `plugin service stop --name <id>` | 结束进程（含子进程） |

启动服务时注入的环境变量：

| 变量 | 含义 |
| --- | --- |
| `MCSM_PLUGIN_ID` | 插件 id |
| `MCSM_PLUGIN_DIR` | 插件安装目录 |
| `MCSM_PLUGIN_DATA` | 插件数据目录（可写） |
| `MCSM_PLUGIN_PORT` | 声明的端口（若 `web.port` 存在） |

界面上的「打开面板」按钮会先 `service start`，再从返回的 `url` 打开浏览器；
若插件只提供静态 `web.entry` 而没有 `service`，则直接用浏览器打开该 HTML 文件。

建议服务只监听 `127.0.0.1`；需要对外访问时请做成显式配置项并在 README 里说明。

---

## 8. 权限、安全与隔离

插件是**受信任扩展**，不是沙箱。安装第三方插件前请确认来源。

| `permissions` 建议值 | 含义 |
| --- | --- |
| `ui.pages` | 注册界面页面 |
| `servers.read` | 读取服务器列表 / 状态 |
| `servers.control` | 启停服务器、发送指令 |
| `config.write` | 写入 `server.properties` |
| `filesystem.data` | 读写插件数据目录 |
| `network.listen` | 对外开放本地端口（Web 面板） |
| `network.outbound` | 访问外网 |

管理器侧的保护措施：

1. 前端脚本运行在 QJSEngine 中，只能通过 `mcsm.*` 暴露的接口访问能力（没有文件系统 / 进程 API）。
2. 后端插件在独立进程里运行并有超时保护；失败只变成 `warnings`，不会中断服务器管理流程。
3. 导入前必须先通过 `plugin package inspect`：id 合法性、`apiVersion`、入口文件存在性都会被校验。
4. 插件只能写入自己的安装目录与 `.data/<id>` 数据目录。

### 8.1 安装位置：只在管理器目录内

* 插件**只能**安装到 `<数据目录>/plugins/<插件 id>/`（Windows 默认
  `%LOCALAPPDATA%\McServerManager\plugins`）。安装前会校验目标目录确实位于该根目录之内，
  越界直接拒绝（`UNSAFE_PLUGIN_PATH`）——恶意插件无法把文件写到系统目录或别处。
* 插件 id 只允许 `A-Za-z0-9._-`，不能包含路径分隔符，所以无法用 `../` 之类的方式跳出插件目录。

### 8.2 压缩包与入口路径校验

| 检查项 | 行为 |
| --- | --- |
| 绝对路径 / 盘符 / UNC（`C:\…`、`\\server\…`、`/etc/…`） | 拒绝解压（提示"压缩包包含不安全路径"） |
| `..` 跳出解压目录（zip-slip） | 拒绝解压 |
| 符号链接指向外部 | 拒绝（解压后逐个校验落盘路径） |
| 解压后逐个校验落盘文件 | 必须仍在插件目录内，否则报错并清理 |
| `frontend` / `backend` / `web` 入口、`web.service` | 必须是包内相对路径，绝对路径或 `..` 直接拒绝 |
| 后端入口、`include()` 的真实路径 | 必须位于该插件目录内（符号链接会被展开检查） |

### 8.3 运行期隔离（一条卡住不会拖死全部）

| 机制 | 说明 |
| --- | --- |
| 前端脚本预算 | 加载 2500ms、单次事件 1000ms；超时会被强制中断 |
| 前端脚本失控 | 连续超预算的插件在**本次会话内停用**（不再接收事件），插件扩展页会给出提示 |
| 后端插件 | 独立进程 + 超时保护；失败只变成 `warnings`，不影响其它操作 |
| 界面线程 | 后端调用全部异步且带看门狗；插件拿到的服务器列表是缓存快照，不会同步等待后端 |
| 定时备份守护进程 | 启动时带 `--parent-pid`，管理器被强杀后守护进程自动退出，不留孤儿进程 |

---

## 9. 版本兼容

* `apiVersion` 是整数区间校验（见 §4.3）：只增加新接口时抬 `SUPPORTED_MAX_API`；
  破坏性变更时抬 `SUPPORTED_MIN_API`，旧插件被明确拒绝而不是行为异常。
* 管理器新增能力时只增加字段、不删除旧字段；插件应当忽略不认识的键。
* 查询当前接口定义（机器可读，适合生成文档或做自动检查）：

```powershell
mcsm-cli plugin api --pretty
```

```json
{
  "apiVersion": 1,
  "supportedMinApi": 1,
  "supportedMaxApi": 1,
  "managerVersion": "1.1.1",
  "apiVersionPolicy": {
    "type": "integer",
    "bumpOn": "每增加一批新接口 +1",
    "rejectWhen": "不在 supportedMinApi..supportedMaxApi 区间内",
    "minManagerVersion": "仅提示用户，不参与硬校验"
  },
  "manager": "McServerManager plugin package API",
  "package": {
    "layout": ["plugin.json", "frontend/index.js", "backend/index.js", "web/index.html", "README.md"],
    "requiredFields": ["id", "name", "version"],
    "apiVersionType": "整数，每增加一批新接口 +1",
    "supportedApiRange": "1-1"
  },
  "security": {
    "installRoot": "C:/Users/<you>/AppData/Local/McServerManager/plugins",
    "installRootPolicy": "插件只能安装在管理器数据目录的 plugins/ 下",
    "archiveRules": ["禁止绝对路径 / 盘符 / UNC", "禁止 .. 跳出解压目录", "禁止符号链接",
                     "解压后逐个校验落盘路径仍在插件目录内"],
    "entryRules": "frontend/backend/web 入口必须是包内相对路径"
  },
  "scopes": { "frontend": "只扩展桌面界面", "backend": "只扩展后端", "web": "支持库 / Web 面板",
              "global": "同时包含前端与后端内容", "auto": "按目录结构自动识别" },
  "frontend": {
    "runtime": "QJSEngine (ES2020, 无 DOM)",
    "globalObject": "mcsm",
    "namespaces": ["mcsm.plugin", "mcsm.ui", "mcsm.storage", "mcsm.settings", "mcsm.servers",
                   "mcsm.backend", "mcsm.events", "mcsm.utils"],
    "pageBlocks": ["heading", "text", "keyvalue", "table", "buttons", "log", "html", "divider", "progress"]
  },
  "backend": {
    "protocol": "JSON lines over stdin/stdout",
    "runtimes": ["node", "python", "exec", "auto"],
    "hooks": ["plugin.install", "plugin.enable", "plugin.disable", "plugin.uninstall",
              "server.beforeStart", "server.afterStart", "server.beforeStop", "server.afterStop",
              "server.beforeBackup", "server.afterBackup", "server.configApplied", "scheduler.tick"],
    "startPatchKeys": ["jvmArgs", "env", "dockerArgs", "note", "cancel", "reason"]
  },
  "cli": { "install": "mcsm-cli plugin package install --zip <file>|--url <url> [--force]" },
  "pluginsDir": "C:/Users/<you>/AppData/Local/McServerManager/plugins"
}
```

---

## 9.1 调试与排错

| 场景 | 做法 |
| --- | --- |
| 只想看识别结果 | `plugin package inspect --zip <file>` |
| 前端脚本报错 | 「设置 -> 插件扩展」状态行会显示 `插件 id：错误`；也可用 `mcsm.log()` 输出 |
| 后端没有响应 | `plugin package info --name <id>` 查看 `launchCommand` 与 `launchError`，再用 `plugin call ... --timeout 5000` 手动测试 |
| 钩子未生效 | 确认 `hooks` 里声明了该名字（支持 `server.*`），且插件处于**已启用**状态 |
| 启动参数没变化 | 查看 `<服务器目录>/.mcsm/start-patch.json`，以及 `start.sh` / `start.cmd` 里的 `[mcsm] plugin tuning` 行 |
| 查看运行日志 | 后端日志 `<数据目录>/logs/backend.log`（含插件错误） |
| 干净重装 | `plugin package remove --name <id> --purge-data` 后重新导入 |

---

## 10. 命令速查

| 命令 | 说明 |
| --- | --- |
| `plugin packages [--enabled]` | 列出插件包（可只列已启用） |
| `plugin package install --zip <file> [--force] [--disabled] [--expect <id>]` | 从本地 zip 安装 |
| `plugin package install --url <url> [--force]` | 从 URL 安装 |
| `plugin package inspect --zip <file>` / `--url <url>` | 只校验并输出识别结果 |
| `plugin package list` / `plugin package info --name <id>` | 列表 / 详情（含启动命令与服务状态） |
| `plugin package enable` / `disable --name <id>` | 启用 / 停用（触发对应钩子） |
| `plugin package remove --name <id> [--purge-data]` | 卸载（触发 `plugin.uninstall`） |
| `plugin call --name <id> --method <m> [--params '{...}'] [--timeout ms]` | 调用后端方法 |
| `plugin hook --name <id>` / `--all --hook <h> [--payload '{...}']` | 手动触发钩子 |
| `plugin service start` / `stop` / `status --name <id>` | 管理 Web 面板服务 |
| `plugin api` | 输出机器可读接口清单 |

---

## 11. 示例插件

| 目录 | 作用域 | 演示内容 |
| --- | --- | --- |
| `examples/plugins/hello-panel` | 前端 | 注册页面、表格、按钮执行后端命令、事件订阅 |
| `examples/plugins/perf-tuner` | 后端 | `server.beforeStart` 按内存追加 JVM 参数并写入启动补丁 |
| `examples/plugins/ops-console` | 全局 | 前端页面 + JSON-Line 后端 + 本地 Web 面板服务 |

打包命令：

```powershell
powershell -File examples\plugins\build-examples.ps1   # 输出到 dist\plugins\
```
