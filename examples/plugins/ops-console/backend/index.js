/*
 * 全局插件的**后端部分**：JSON-Line 协议，响应管理器发来的调用与钩子。
 */
const os = require("os");
const readline = require("readline");

function reply(object) {
    process.stdout.write(JSON.stringify(object) + "\n");
}

const rl = readline.createInterface({ input: process.stdin });
rl.on("line", (line) => {
    let request = {};
    try {
        request = JSON.parse(line);
    } catch (error) {
        return reply({ ok: false, error: { code: "BAD_JSON", message: String(error) } });
    }

    switch (request.method) {
    case "summary":
        return reply({
            ok: true,
            data: {
                host: os.hostname(),
                platform: os.platform() + " " + os.release(),
                cpus: os.cpus().length,
                freeMemGb: Math.round(os.freemem() / 1024 / 1024 / 1024),
                manager: request.apiVersion
            }
        });
    case "plugin.install":
    case "plugin.enable":
        return reply({ ok: true, data: { acknowledged: true } });
    case "server.afterStart":
    case "server.afterStop":
        return reply({
            ok: true,
            data: { event: request.method },
            log: [request.method + " " + ((request.params.server || {}).id || "")]
        });
    default:
        return reply({
            ok: false,
            error: { code: "METHOD_NOT_FOUND", message: "未实现的方法：" + request.method }
        });
    }
});
