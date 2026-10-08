/*
 * 全局插件的**前端部分**：与后端部分配合工作。
 * mcsm.backend.invoke 可以直接调用本插件自己的后端方法：
 *   mcsm.backend.invoke("plugin call --name com.example.ops-console --method summary", ...)
 */
(function () {
    var pluginId = mcsm.plugin.id;
    var pageId = mcsm.ui.registerPage({
        id: "ops-console",
        title: "运维面板",
        icon: "⛭",
        subtitle: "插件提供的本地 Web 运维面板入口"
    });

    function render(text) {
        mcsm.ui.setPageContent(pageId, [
            { type: "heading", text: "运维面板" },
            {
                type: "text",
                style: "hint",
                text: "Web 部分由插件自带的本地 HTTP 服务提供，点击下方按钮启动服务并打开浏览器面板。"
            },
            { type: "keyvalue", items: [["插件", pluginId], ["面板端口", "18765"]] },
            { type: "buttons", items: [
                { label: "汇总信息", command: "plugin call --name " + pluginId + " --method summary", toast: "已获取" },
                { label: "启动面板服务", command: "plugin service start --name " + pluginId, toast: "服务已启动" },
                { label: "打开面板", command: "plugin service status --name " + pluginId,
                  openUrlFrom: "url", toast: "正在打开面板" }
            ] },
            { type: "log", text: text || "等待操作…" }
        ]);
    }

    mcsm.events.on("app.ready", function () { render(); });
    render();
})();
