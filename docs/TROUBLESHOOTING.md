# 常见问题与排查

## 环境相关

**提示「未找到后端程序 mcsm-cli」**

- 便携版：确认 `mcsm-cli.exe` 与 `McServerManager.exe` 在同一目录
- 源码构建：先编译 `backend` 目标（`cmake --build build --target mcsm-cli`）
- 也可以在「设置 → 运行环境 → 后端程序」里手动指定路径，或设置环境变量 `MCSM_BACKEND`

**Docker 状态显示未运行，但我已经启动了 Docker Desktop**

状态每 20 秒自动重检一次，也可以点「重新检测环境」立即刷新。若长时间不恢复，在命令行执行
`mcsm-cli doctor` 查看具体错误（例如命名管道未就绪、Docker Desktop 正在启动）。

**拉取 JDK 镜像很慢或失败**

国内网络建议给 Docker 配置镜像加速：Docker Desktop → Settings → Docker Engine，加入
`"registry-mirrors": ["https://docker.m.daocloud.io"]` 之类的地址后重启 Docker。
注意 `docker manifest inspect` 这类命令**不走**加速器，会表现为超时，但 `docker pull` 正常。

**MinGW 编译时 `cc1plus.exe` 以 `0xC0000135` 退出**

编译器找不到自己的 DLL，把 `D:\qt\Tools\mingw1310_64\bin` 加入 `PATH` 即可；
仓库里的 `scripts/build-windows.ps1` 会自动处理。

## 服务器相关

**启动失败提示 `UnsupportedClassVersionError`**

JDK 版本低于服务端要求。在「设置 → 实验性功能」关闭的情况下，重建服务器会自动匹配；
也可以在「服务器」详情里用 `server update --java 21` 调整镜像（下次启动会自动拉取）。

**启动失败提示端口被占用**

`-p <port>:<port>` 映射失败（宿主机端口已被别的程序或另一台服务器占用）。
换一个端口重建服务器，或先释放占用该端口的进程。

**服务端文件损坏 / 不是可执行服务端**

启动时日志会出现 `Invalid or corrupt jarfile`，管理器会在几秒内识别并返回该提示。
重新执行安装（会重新下载）或在服务器目录手动放入正确的 jar。

**RCON 指令发送失败**

- 服务器必须处于运行状态
- `server.properties` 需要 `enable-rcon=true`，且记录里的 `rcon.port` / `rcon.password` 与文件一致
  （管理器创建服务器时会自动写入；手动改过配置页的话请注意同步）
- RCON 端口只绑定在 `127.0.0.1`，仅本机可用

**备份失败提示缺少 tar**

Windows 10 1803 以上自带 `tar`；若被精简掉，可让服务器保持运行（此时使用容器内的 tar），
或安装 7-Zip 后重试。

**恢复备份后插件消失了**

默认只回滚世界存档并保留插件与配置；如果该归档里确实包含 `plugins/`，用
`mcsm-cli backup restore --scope all` 才会连同插件一起回滚。

## 界面相关

**下拉列表出现过黑底黑字**

早期版本只设置了样式表，导致下拉弹出层跟随 Windows 系统主题。现在程序会同时设置调色板、
使用 Fusion 样式并为每个弹出列表单独设置样式，问题已修复；若你仍看到异常，请附截图。

**日志里出现大量 `>`**

这只在实验性「本机 JDK」模式下可能出现：服务端进程的 stdin 被关闭时 Minecraft 会不断打印提示符。
当前版本用一条常驻 `ping` 保持 stdin 打开，日志里只会有正常的提示符；若再次出现，说明该服务器的
`start.cmd` 是旧版本生成的，删掉它重新启动一次即可（管理器每次启动都会重新生成）。

**用脚本调用 `mcsm-cli server start` 一直不返回**

实验性「本机 JDK」模式下的服务端是本机进程，启动器会继承调用者的输出句柄。
如果你的脚本要读取管道直到 EOF，就会一直等到服务器退出；改成把输出重定向到文件，或直接看退出码即可。
GUI 不受影响。

**关闭应用后仍有 mcsm-cli 进程**

正常关闭（点窗口右上角 ✕）会一并结束后台定时备份守护进程；用任务管理器强制结束应用时不会触发该流程，
残留的守护进程可以手动结束，或在下次启动应用时被新的守护进程取代（同一时刻建议只保留一个）。

## 诊断工具

```bash
mcsm-cli doctor                  # 环境自检（Docker / JDK 镜像 / tar / 数据目录）
python tools/check-endpoints.py  # 上游 API 是否可用（Mojang / Paper / Purpur / Fabric / Modrinth / Hangar）
python tools/rcon-probe.py 127.0.0.1 25575 <rcon密码> list   # RCON 原始报文探针
tail -f <数据根>/logs/backend.log  # 后端诊断日志
```
