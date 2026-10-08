/*
 * 前端插件示例（运行在 QJSEngine 中，没有 DOM）。
 *
 * 可用对象：
 *   mcsm.plugin.{id,name,version,scope}   mcsm.log / mcsm.toast
 *   mcsm.ui.registerPage / setPageContent / openPage
 *   mcsm.servers.list / selected
 *   mcsm.backend.invoke(命令, 回调)        mcsm.events.on(事件, 回调)
 *   mcsm.storage.get/set                  mcsm.settings.get/set
 */
(function () {
    var pageId = mcsm.ui.registerPage({
        id: "hello-panel",
        title: "示例面板",
        icon: "★",
        subtitle: "由插件 " + mcsm.plugin.id + " 提供"
    });

    function block(status) {
        var servers = mcsm.servers.list() || [];
        var rows = servers.map(function (server) {
            return [server.name || server.id, server.type || "-", status || server.statusText || "-"];
        });

        mcsm.ui.setPageContent(pageId, [
            { type: "heading", text: "你好，" + mcsm.plugin.name + " v" + mcsm.plugin.version },
            {
                type: "text",
                style: "hint",
                text: "这个页面完全由插件脚本生成，可以读取服务器列表、调用后端命令并弹出通知。"
            },
            { type: "keyvalue", items: [
                ["插件 ID", mcsm.plugin.id],
                ["作用域", mcsm.plugin.scope],
                ["管理器版本", mcsm.version],
                ["服务器数量", String(servers.length)]
            ] },
            { type: "table", columns: ["名称", "类型", "状态"], rows: rows },
            { type: "buttons", items: [
                { label: "刷新服务器列表", command: "server list", toast: "已刷新服务器列表" },
                { label: "弹出通知", toast: "这是插件发出的通知", level: "success" }
            ] },
            { type: "divider" },
            {
                type: "buttons",
                items: [
                    { label: "查询后端版本", command: "version", toast: "查询完成" }
                ]
            },
            { type: "text", style: "hint", text: "最近一次操作状态：" + (status || "等待操作") }
        ]);
    }

    // 事件订阅：管理器刷新服务器列表或选中服务器时自动重绘
    mcsm.events.on("servers.refreshed", function () { block("服务器列表已更新"); });
    mcsm.events.on("server.selected", function (payload) {
        block("当前选中：" + (payload && payload.serverId ? payload.serverId : "-"));
    });

    // 主动查询一次后端，演示 invoke + 回调
    mcsm.backend.invoke("server list", function (reply) {
        if (reply.ok) {
            block("已从后端读取 " + ((reply.data.servers || []).length) + " 台服务器");
        } else {
            block("后端调用失败：" + reply.error);
        }
    });

    block();
    mcsm.log("hello-panel 已加载");
})();
