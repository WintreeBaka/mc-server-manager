# 更新日志

## 1.1.1

### 接口版本（apiVersion）改为整数区间

* `apiVersion` 现在是**整数**：管理器每增加一批新接口 `+1`。
* 管理器维护 `SUPPORTED_MIN_API` / `SUPPORTED_MAX_API`（当前均为 `1`）：
  插件的 `apiVersion` 不在区间内会**拒绝加载**，日志与界面写明原因。
* 破坏性变更时抬高 `SUPPORTED_MIN_API`，旧插件自然淘汰，而不是行为异常。
* `minManagerVersion` 只作为**用户可见的提示**（版本偏低时给出警告），不参与硬校验。
* 兼容：写成字符串 `"1"` 仍能加载，但会提示改成整数；示例插件已改为整数写法。
* `mcsm-cli plugin api` 现在输出 `apiVersion`（整数）、`supportedMinApi`、`supportedMaxApi`、
  `managerVersion`、`apiVersionPolicy` 与 `security` 策略块。

### 安全：插件只能装在管理器目录里

* 安装目标必须位于 `<数据目录>/plugins/` 之内，越界直接拒绝（`UNSAFE_PLUGIN_PATH`）。
* 解压前扫描压缩包条目：**绝对路径 / 盘符 / UNC / `..` 跳出目录**全部拒绝（zip-slip 防护）。
* 解压后逐个校验落盘路径仍在插件目录内，并拒绝符号链接。
* `frontend` / `backend` / `web` 入口与 `web.service` 必须是包内相对路径；
  后端入口与前端 `mcsm.include()` 都会展开真实路径再校验，防止借助 `..` 或链接执行外部程序。

### 界面性能与"不互相拖死"

* 页面切换不再对整页做 `grab()` 快照，连点菜单时直接瞬切（不再逐帧动画）——这是卡顿的主因。
* 窗口背景（渐变 + 两团柔光）改为缓存位图，只在尺寸/主题变化时重绘。
* 页面激活时的刷新全部**节流**：`doctor`（会调用 docker CLI）4 秒内不重复执行，
  各页面列表/详情 1.5–2.5 秒内不重复拉取；后台页面不再因轮询而重建控件。
* 修复信号连接泄漏：设置页每次进入都会再连一次 `environmentChanged`，环境更新被重复执行 N 次。
* 服务器列表在内容未变化时不再触发模型信号（轮询不再引起全界面重建与重绘）。
* 隐藏页面会释放资源：控制台页离开 5 秒后停止 `docker logs --follow`；服务器页隐藏时不重建列表。
* 插件前端脚本加**看门狗**：加载 2500ms / 单次事件 1000ms 预算，超时强制中断并临时停用该插件；
  插件拿到的服务器列表改为缓存快照，消除"插件一调用就卡住整个窗口"的同步等待。
* 后端异步请求加看门狗（15 分钟兜底），杜绝"一条命令卡住 → 回调永不返回 → 该功能永久卡死"。
* 定时备份守护进程带 `--parent-pid`：管理器被强杀后守护进程自动退出，不再残留孤儿子进程。

### 实测（12 逻辑核，快速轮切 7 个页面）

| 场景 | 1.1.0 | 1.1.1 |
| --- | --- | --- |
| 空闲 | 0.29% | **0.03%** |
| 120ms 间隔轮切（53 次） | 2.20% | **1.15%** |
| 60ms 间隔轮切（95 次） | 2.91% | **1.76%** |

新增 `tools/ui-perf.ps1` 复现上述数据；`tools/smoke-test.ps1` 增加 10 项安全 / apiVersion 回归（共 67 项）。

## 1.1.0

### 新增：管理器插件包（Plugin SDK）

* **插件导入**：设置 → 插件扩展 → 添加插件，支持**本地 zip** 与 **URL** 两种来源，
  导入前先校验并展示识别到的作用域。命令行等价入口为 `mcsm-cli plugin package install|inspect`。
* **内置 7-Zip**：解压使用随程序发布的 `third_party/7zip/7za.exe`，目标机器不需要安装任何压缩软件
  （缺失时回退系统 7-Zip / tar）。7-Zip 的 LGPL 许可文本随包分发。
* **作用域自动识别**：按 `plugin.json` 的 `scope` 或目录结构自动判定
  `frontend` / `backend` / `web` / `global`，识别失败会拒绝导入并给出原因。
* **前端扩展接口**：`QJSEngine` 宿主 + `mcsm.*` API
  （`ui.registerPage` / `ui.setPageContent` / `toast` / `storage` / `settings` / `servers` /
  `backend.invoke` / `events.on` / `include`），页面由
  `heading / text / keyvalue / table / buttons / log / html / divider / progress` 区块描述，
  自动跟随主题、字体与配色；插件页面会出现在左侧主菜单中。
* **后端扩展接口**：独立进程 + **JSON-Line** 协议，支持 `node` / `python` / `java` / `exec` /
  `auto` 运行时与超时保护；钩子覆盖 `plugin.install|enable|disable|uninstall`、
  `server.beforeStart|afterStart|beforeStop|afterStop|beforeBackup|afterBackup|configApplied`、
  `scheduler.tick`，并支持 `server.*` 通配。
* **性能优化类插件**：`server.beforeStart` 可返回启动补丁
  （`jvmArgs` / `env` / `dockerArgs` / `note`，或用 `cancel` + `reason` 阻止启动），
  合并结果写入 `<服务器目录>/.mcsm/start-patch.json` 并在容器参数与生成的启动脚本中生效；
  停用插件后补丁自动清除。
* **支持库 / Web 面板类插件**：`plugin service start|stop|status` 管理插件自带的本地 HTTP 服务，
  注入 `MCSM_PLUGIN_ID` / `MCSM_PLUGIN_DIR` / `MCSM_PLUGIN_DATA` / `MCSM_PLUGIN_PORT`，
  界面上的「打开面板」按钮可一键启动并打开浏览器。
* **接口文档**：新增 `docs/PLUGIN-SDK.md`（包结构、`plugin.json` 全字段、前后端 API、钩子与补丁、
  权限说明、版本兼容、调试方法、命令速查），并可随时用 `mcsm-cli plugin api` 导出机器可读清单。
* **示例插件**：`examples/plugins/hello-panel`（前端）、`perf-tuner`（后端）、`ops-console`（全局 + Web 面板），
  以及打包脚本 `examples/plugins/build-examples.ps1`。

### 改进

* 数据根目录（`--home` / `MCSM_HOME`）现在始终解析为绝对路径，注册表与插件路径不再依赖当前工作目录。
* `Json::parseObject` 兼容带 UTF-8 BOM 的 JSON（Windows 工具与 PowerShell 写出的 `plugin.json` 可直接使用）。
* 设置页新增「插件扩展」分类（非实验性功能）。
* 界面深链接增加 `--page settings-plugins` / `extensions` / `settings-runtime`，并支持直接写插件页面 id。

### 测试

* `tools/smoke-test.ps1` 增加插件包回归：现场打包前端 / 后端 / 全局三种插件，
  覆盖「校验识别 → 安装 → 重复安装拒绝 → --force 覆盖 → 列表 → 后端 JSON-Line 调用 → 启停 → 卸载 → 非法包拒绝」。
* GUI 冒烟测试新增插件页面验证（`--page hello-panel`、`--page settings-plugins`）。

## 1.0.0

* 首个版本：Docker 化 JDK 运行环境、一键自动配置（Paper / Purpur / 原版 / Fabric）、
  控制台（日志 + RCON）、配置双模式与回滚、定时备份、Minecraft 插件市场、
  暗黑 / 亮色与渐变 / 纯色主题、贝塞尔过渡动画、字体预设、实验性本机 JDK 模式。
