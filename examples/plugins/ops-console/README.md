# 运维面板示例（ops-console）

**全局插件**示例：同时包含

- `frontend/` —— 在前端注册「运维面板」页面
- `backend/` —— JSON-Line 后端，提供 `summary` 方法与生命周期钩子
- `web/` —— 支持库 / Web 面板静态资源，由 `backend/server.js` 提供服务
