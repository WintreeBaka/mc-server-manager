# 性能调优示例（perf-tuner）

**后端插件**示例，演示最实用的扩展点：`server.beforeStart` 钩子返回 `patch`，
管理器会把 `jvmArgs` 合并进容器的 `JAVA_OPTS`（容器模式）或本机启动脚本（本机 JDK 模式）。

12G 内存以上会追加 ZGC 参数；结果写入 `<服务器目录>/.mcsm/start-patch.json`，
`start.sh` 里也会打印一行 `[mcsm] plugin tuning: <插件 id>`，方便确认生效。
