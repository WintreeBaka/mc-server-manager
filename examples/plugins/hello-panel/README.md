# 示例面板（hello-panel）

最小可用的**前端扩展插件**：注册一个页面，用表格展示服务器列表，提供按钮调用后端命令，
并订阅 `servers.refreshed` / `server.selected` 事件自动重绘。

## 打包与安装

```powershell
# 打包（使用随管理器发布的内置 7-Zip）
powershell -File examples\plugins\build-examples.ps1

# 安装
build\bin\mcsm-cli.exe plugin package install --zip dist\plugins\com.example.hello-panel-1.0.0.zip
```

安装后在「设置 → 插件扩展」中确认状态为“已启用”，左侧菜单会出现「★ 示例面板」。
