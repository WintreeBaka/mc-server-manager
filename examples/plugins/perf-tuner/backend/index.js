/*
 * 后端插件示例（Node.js）。
 *
 * 协议：管理器把一行 JSON 请求写到 stdin，插件把一行 JSON 结果写到 stdout。
 * 请求：{"apiVersion":"1","plugin":"<id>","method":"server.beforeStart","params":{...}}
 * 结果：{"ok":true,"data":{...},"patch":{"jvmArgs":["-XX:+UseZGC"]}}
 *
 * patch 支持的键：jvmArgs[] / env{} / dockerArgs[] / note / cancel / reason
 */
const readline = require("readline");

function reply(object) {
    process.stdout.write(JSON.stringify(object) + "\n");
}

function memoryGb(server) {
    const raw = String((server && server.memory) || "4G").toUpperCase().replace("G", "");
    const value = parseFloat(raw);
    return isNaN(value) ? 4 : value;
}

const rl = readline.createInterface({ input: process.stdin });
rl.on("line", (line) => {
    let request = {};
    try {
        request = JSON.parse(line);
    } catch (error) {
        return reply({ ok: false, error: { code: "BAD_JSON", message: String(error) } });
    }

    const server = (request.params && request.params.server) || {};
    const gb = memoryGb(server);

    if (request.method === "server.beforeStart") {
        const jvmArgs = ["-XX:+AlwaysPreTouch", "-XX:+UseG1GC"];
        if (gb >= 12) {
            jvmArgs.push("-XX:+UseZGC", "-XX:+ZGenerational");
        }
        return reply({
            ok: true,
            data: { memoryGb: gb, serverId: server.id || null },
            patch: {
                jvmArgs: jvmArgs,
                note: "perf-tuner: 已为 " + gb + "G 内存追加 " + jvmArgs.length + " 个 JVM 参数"
            },
            log: ["applied tuning for " + (server.id || "unknown")]
        });
    }

    if (request.method === "server.afterStart") {
        return reply({ ok: true, data: { observed: true }, log: ["server started"] });
    }

    return reply({
        ok: false,
        error: { code: "METHOD_NOT_FOUND", message: "未实现的方法：" + request.method }
    });
});
