/*
 * Web 支持库部分：一个独立的本地 HTTP 服务（管理器用 `plugin service start` 启动它）。
 * 端口从 MCSM_PLUGIN_PORT 读取，静态页面在同级 ../web 目录。
 */
const http = require("http");
const fs = require("fs");
const path = require("path");

const port = parseInt(process.env.MCSM_PLUGIN_PORT || "18765", 10);
const webRoot = path.resolve(__dirname, "..", "web");

const server = http.createServer((request, response) => {
    const target = request.url === "/" ? "/index.html" : request.url.split("?")[0];
    const file = path.join(webRoot, path.normalize(target).replace(/^([/\\])+/, ""));
    if (!file.startsWith(webRoot) || !fs.existsSync(file)) {
        response.writeHead(404, { "Content-Type": "text/plain; charset=utf-8" });
        return response.end("not found");
    }
    response.writeHead(200, { "Content-Type": file.endsWith(".html") ? "text/html; charset=utf-8"
                                                                            : "text/plain; charset=utf-8" });
    fs.createReadStream(file).pipe(response);
});

server.listen(port, "127.0.0.1", () => {
    process.stdout.write(JSON.stringify({ ok: true, data: { url: "http://127.0.0.1:" + port } }) + "\n");
});
