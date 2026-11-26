// Separate Worker: it never modifies the existing website Worker.
import { DurableObject } from "cloudflare:workers";

const PORT = 8080;
const INACTIVITY_MS = 120_000;

function equalsBytes(a, b) {
  if (a.length !== b.length) return false;
  let mismatch = 0;
  for (let i = 0; i < a.length; i++) mismatch |= a[i] ^ b[i];
  return mismatch === 0;
}

async function authorized(request, configured) {
  const header = request.headers.get("Authorization") || "";
  if (!header.startsWith("Bearer ")) return false;
  const token = header.slice(7).trim();
  const dot = token.lastIndexOf(".");
  if (dot < 1 || dot === token.length - 1) return false;
  const actor = token.slice(0, dot);
  const entries = (configured || "").split(";");
  const entry = entries.find((item) => item.slice(0, item.indexOf("=")).trim().toLowerCase() === actor.toLowerCase());
  const hash = entry?.slice(entry.indexOf("=") + 1).trim().toLowerCase();
  if (!hash || !/^[0-9a-f]{64}$/.test(hash)) return false;
  const supplied = new Uint8Array(await crypto.subtle.digest("SHA-256", new TextEncoder().encode(token.slice(dot + 1))));
  const expected = Uint8Array.from(hash.match(/../g), (hex) => parseInt(hex, 16));
  return equalsBytes(supplied, expected);
}

export class AdminContainer extends DurableObject {
  starting;

  async fetch(request) {
    if (new URL(request.url).pathname === "/__internal/audit") return this.audit(request);
    this.starting ??= this.startContainer().finally(() => { this.starting = undefined; });
    await this.starting;
    const url = new URL(request.url);
    url.protocol = "http:";
    url.host = "container";
    const forwarded = new Request(url, request);
    forwarded.headers.delete("host");
    forwarded.headers.set("X-Omni-Container-Proxy", this.env.OMNIGHOST_ADMIN_CONTAINER_PROXY_SECRET);
    return this.ctx.container.getTcpPort(PORT).fetch(forwarded);
  }

  async startContainer() {
    const container = this.ctx.container;
    const { OMNIGHOST_ADMIN_KEYAUTH_SELLER_KEY, OMNIGHOST_ADMIN_API_KEY_HASHES, OMNIGHOST_ADMIN_CONTAINER_PROXY_SECRET, OMNIGHOST_ADMIN_AUDIT_SECRET, OMNIGHOST_ADMIN_AUDIT_URL } = this.env;
    if (!OMNIGHOST_ADMIN_KEYAUTH_SELLER_KEY || !OMNIGHOST_ADMIN_API_KEY_HASHES || !OMNIGHOST_ADMIN_CONTAINER_PROXY_SECRET || !OMNIGHOST_ADMIN_AUDIT_SECRET || !OMNIGHOST_ADMIN_AUDIT_URL)
      throw new Error("Administrative service is not configured");
    if (!container.running) {
      container.start({ image: container.images.base, instance: "lite", enableInternet: true, env: {
        ASPNETCORE_ENVIRONMENT: "Production", ASPNETCORE_URLS: "http://+:8080",
        OMNIGHOST_ADMIN_KEYAUTH_SELLER_KEY, OMNIGHOST_ADMIN_API_KEY_HASHES, OMNIGHOST_ADMIN_CONTAINER_PROXY_SECRET, OMNIGHOST_ADMIN_AUDIT_SECRET, OMNIGHOST_ADMIN_AUDIT_URL,
      }});
    }
    await container.setInactivityTimeout(INACTIVITY_MS);
    const port = container.getTcpPort(PORT);
    for (let attempt = 0; attempt < 100; attempt++) {
      try {
        const response = await port.fetch("http://container/healthz", { headers: { "X-Omni-Container-Proxy": OMNIGHOST_ADMIN_CONTAINER_PROXY_SECRET }, signal: AbortSignal.timeout(1000) });
        await response.body?.cancel();
        if (response.ok) return;
      } catch {}
      await scheduler.wait(200);
    }
    throw new Error("Administrative service is unavailable");
  }

  async audit(request) {
    const actual = request.headers.get("X-Omni-Audit-Secret") || "";
    const expected = this.env.OMNIGHOST_ADMIN_AUDIT_SECRET || "";
    if (!expected || !equalsBytes(new TextEncoder().encode(actual), new TextEncoder().encode(expected)))
      return new Response("Unauthorized", { status: 401 });
    if (request.method === "GET") {
      const requested = Number(new URL(request.url).searchParams.get("limit") || "100");
      const limit = Number.isFinite(requested) ? Math.max(1, Math.min(1000, Math.floor(requested))) : 100;
      const entries = await this.ctx.storage.list({ prefix: "audit:", reverse: true, limit });
      return Response.json([...entries.values()].reverse(), { headers: { "Cache-Control": "no-store" } });
    }
    if (request.method !== "POST") return new Response("Method not allowed", { status: 405 });
    if (Number(request.headers.get("content-length") || 0) > 8192) return new Response("Too large", { status: 413 });
    const text = await request.text();
    if (text.length > 8192) return new Response("Too large", { status: 413 });
    let record;
    try { record = JSON.parse(text); } catch { return new Response("Bad request", { status: 400 }); }
    if (!record || typeof record.hash !== "string" || typeof record.previousHash !== "string" || !/^[0-9a-f]{64}$/.test(record.hash) || !/^[0-9a-f]{64}$/.test(record.previousHash))
      return new Response("Bad request", { status: 400 });
    // Serialize chain updates across overlapping requests and container restarts.
    return this.ctx.blockConcurrencyWhile(async () => {
      const previous = await this.ctx.storage.list({ prefix: "audit:", reverse: true, limit: 1 });
      const last = [...previous.values()][0];
      if (record.previousHash !== (last?.hash || "0".repeat(64))) return new Response("Audit chain mismatch", { status: 409 });
      const seq = (await this.ctx.storage.get("audit-sequence") || 0) + 1;
      await this.ctx.storage.put({ [`audit:${String(seq).padStart(16, "0")}`]: record, "audit-sequence": seq });
      if (seq > 1000) await this.ctx.storage.delete(`audit:${String(seq - 1000).padStart(16, "0")}`);
      return new Response(null, { status: 204 });
    });
  }
}

export default {
  async fetch(request, env) {
    const url = new URL(request.url);
    if (url.pathname === "/healthz") return Response.json({ status: "worker-ready" });
    if (url.pathname === "/__internal/audit") {
      const actual = request.headers.get("X-Omni-Audit-Secret") || "";
      const expected = env.OMNIGHOST_ADMIN_AUDIT_SECRET || "";
      if (!expected || !equalsBytes(new TextEncoder().encode(actual), new TextEncoder().encode(expected))) return new Response("Unauthorized", { status: 401 });
      return env.ADMIN_CONTAINER.getByName("primary").fetch(request);
    }
    if (!url.pathname.startsWith("/api/admin/")) return new Response("Not found", { status: 404 });
    if (!(await authorized(request, env.OMNIGHOST_ADMIN_API_KEY_HASHES)))
      return new Response("Unauthorized", { status: 401, headers: { "Cache-Control": "no-store" } });
    try { return await env.ADMIN_CONTAINER.getByName("primary").fetch(request); }
    catch { return new Response("Administrative service unavailable", { status: 503, headers: { "Cache-Control": "no-store" } }); }
  },
};
