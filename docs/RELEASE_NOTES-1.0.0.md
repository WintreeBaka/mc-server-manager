# McServerManager 1.0.0

Qt 6 + C++ 写的 Minecraft 服务器管理器：**桌面界面 + CLI 后端分离，服务端跑在 Docker 容器里**，宿主机不需要安装 Java。

## 下载

| 文件 | 说明 |
| --- | --- |
| `McServerManager-1.0.0-win64.zip` | Windows 10/11 64 位便携版，解压即用，**不需要安装 Qt 或 Java** |

> 使用前请先安装并启动 [Docker Desktop](https://www.docker.com/products/docker-desktop/)。
> 国内网络建议在 Docker 设置里配置镜像加速（`registry-mirrors`），否则拉取 JDK 镜像会很慢。

### 校验

```
解压后目录结构：
  McServerManager.exe     桌面界面
  mcsm-cli.exe            后端 CLI（界面会自动找到同目录的这一份）
  Qt6*.dll, platforms/    运行库，勿删除
  QUICKSTART.txt          中文上手指南
  README.md               完整文档
```

自包含性已验证：把压缩包解压到任意目录，在仅含系统目录的 `PATH` 下运行 `McServerManager.exe`
与 `mcsm-cli.exe doctor` 均正常。

## 亮点

- **一键自动配置**：选好类型（Paper / Purpur / 原版 / Fabric）与版本，自动下载服务端、拉取匹配的 JDK 镜像、
  生成配置并启动；失败时给出可读原因（端口占用 / JDK 不匹配 / EULA 未同意 / 内存不足 / 服务端损坏…）
- **JDK 容器化**：每个服务器一个容器，按版本自动匹配（26.x→JDK 25、1.21→21、1.20.4→17、1.16.5→8）
- **控制台**：实时日志跟随（按等级着色）+ RCON 指令，内置常用快捷指令
- **配置文件**：快捷表单与专家模式双模式编辑，保留注释；每次保存自动留档，可精确回滚
- **备份**：定时自动备份（间隔 / 保留份数 / 含插件 / 备份前 `save-all`）、手动快照、一键恢复
- **插件市场**：Modrinth / Hangar 搜索并一键安装，支持启停与删除
- **界面**：暗黑 / 亮色主题，马卡龙粉-蓝配色，**渐变与纯色两种风格**，贝塞尔缓动动画（速度可调），
  字体预设与自定义字体，无边框窗口与平滑页面切换
- **实验性功能**：扫描本机 JDK，创建服务器时可克隆本机 JDK 直接运行（不经过容器）

## 快速开始

1. 安装并启动 Docker Desktop
2. 解压压缩包，双击 `McServerManager.exe`
3. 首页点「新建服务器」→ 选择类型与版本 → 开始创建（进度条会显示下载与镜像拉取进度）
4. 回到「服务器」页点「一键启动」，首次启动需要生成世界，通常 30–120 秒

## 运行环境

- Windows 10 / 11 64 位
- Docker Desktop（必需）
- 首次创建服务器需要联网（服务端约 50 MB，JDK 镜像约 200–450 MB）

## 已知边界

- 插件一键安装仅支持 Bukkit 系（Paper / Purpur）；原版没有插件 API，Fabric 请把模组放进 `mods/`
- SpigotMC 需要浏览器会话才能下载，该来源只提供搜索与页面跳转
- Forge / NeoForge 未纳入自动配置，可用「手动导入」加载已构建好的服务端
- 定时备份依赖应用运行（启动时会拉起 `mcsm-cli daemon`），需要 7×24 时可注册为系统服务
- 实验性「本机 JDK」模式下服务端是本机进程；用「读到 EOF」的管道方式调用 `mcsm-cli server start`
  会等到服务器退出才返回（GUI 不受影响）

## 本次验证

- 静态检查 92 个源文件：0 错误 0 警告
- 后端冒烟测试 37 项：全部通过（不需要 Docker）
- 界面冒烟测试：启动、窗口拖拽、连续 8 次快速切页后像素级 0 残留、关闭后无残留进程
- 真实服务器全流程：Paper 1.21.8 自动安装 → 启动到 `Done` → RCON 指令 → 热备份 → 插件安装并加载 →
  存档恢复 → 定时备份与保留策略 → 三类故障诊断（服务端损坏 / EULA 未同意 / 端口占用）
- 从仓库目录全新构建：53/53 目标成功

## 反馈

遇到问题请附上 `<数据目录>/logs/backend.log`（Windows 默认 `%LOCALAPPDATA%\McServerManager\logs`）
和界面截图，也欢迎直接用仓库自带的诊断工具输出：

```bash
mcsm-cli doctor
python tools/check-endpoints.py
```
