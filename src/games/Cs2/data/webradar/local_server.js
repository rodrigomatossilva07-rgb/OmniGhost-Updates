"use strict";
const http = require("http");
const fs = require("fs");
const path = require("path");
const root = __dirname;
const port = Number(process.env.OMNIGHOST_CS2_RADAR_PREVIEW_PORT || 8878);
const mime = { ".html":"text/html; charset=utf-8", ".js":"application/javascript; charset=utf-8",
  ".css":"text/css; charset=utf-8", ".json":"application/json; charset=utf-8",
  ".png":"image/png", ".svg":"image/svg+xml" };
http.createServer((request, response) => {
  const pathname = decodeURIComponent(new URL(request.url, "http://127.0.0.1").pathname);
  const relative = pathname === "/" ? "index.html" : pathname.replace(/^\/+/, "");
  const file = path.resolve(root, relative);
  if (!file.startsWith(root + path.sep) || !fs.existsSync(file) || !fs.statSync(file).isFile()) {
    response.writeHead(404, {"content-type":"text/plain; charset=utf-8"});
    return response.end("Not found");
  }
  response.writeHead(200, {"content-type":mime[path.extname(file).toLowerCase()] || "application/octet-stream",
    "cache-control":"no-store", "x-content-type-options":"nosniff"});
  fs.createReadStream(file).pipe(response);
}).listen(port, "127.0.0.1", () =>
  console.log(`OmniGhost CS2 Radar: http://127.0.0.1:${port}/#preview-local`));
