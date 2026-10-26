"use strict";
const http = require("http");
const fs = require("fs");
const path = require("path");
const root = __dirname;
const port = Number(process.env.OMNIGHOST_RADAR_PREVIEW_PORT || 8877);
const logo = path.resolve(root, "../../../../../resources/OmniGhost.png");
const routes = new Map([
  ["/", [path.join(root, "index.html"), "text/html; charset=utf-8"]],
  ["/index.html", [path.join(root, "index.html"), "text/html; charset=utf-8"]],
  ["/app.js", [path.join(root, "app.js"), "application/javascript; charset=utf-8"]],
  ["/styles.css", [path.join(root, "styles.css"), "text/css; charset=utf-8"]],
  ["/manifest.json", [path.join(root, "manifest.json"), "application/manifest+json"]],
  ["/sw.js", [path.join(root, "sw.js"), "application/javascript; charset=utf-8"]],
  ["/omnighost.png", [logo, "image/png"]]
]);
http.createServer((request, response) => {
  const pathname = new URL(request.url, "http://127.0.0.1").pathname;
  const route = routes.get(pathname);
  if (!route || !fs.existsSync(route[0])) {
    response.writeHead(404, { "content-type": "text/plain; charset=utf-8" });
    return response.end("Not found");
  }
  response.writeHead(200, { "content-type": route[1], "cache-control": "no-store", "x-content-type-options": "nosniff" });
  fs.createReadStream(route[0]).pipe(response);
}).listen(port, "127.0.0.1", () => console.log(`OmniGhost FiveM Radar: http://127.0.0.1:${port}/#preview-local`));
