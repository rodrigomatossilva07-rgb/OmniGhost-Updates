import { test } from "node:test";
import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import { webcrypto } from "node:crypto";

globalThis.crypto ??= webcrypto;
globalThis.DurableObject = class { constructor(ctx, env) { this.ctx = ctx; this.env = env; } };
const source = (await readFile(new URL("./cloudflare-admin.mjs", import.meta.url), "utf8"))
  .replace('import { DurableObject } from "cloudflare:workers";', "const DurableObject = globalThis.DurableObject;");
const { default: worker, AdminContainer } = await import(`data:text/javascript;base64,${Buffer.from(source).toString("base64")}`);

test("rejects invalid administrator credentials before starting a container", async () => {
  const digest = Buffer.from(await webcrypto.subtle.digest("SHA-256", new TextEncoder().encode("secret"))).toString("hex");
  let calls = 0;
  const env = { OMNIGHOST_ADMIN_API_KEY_HASHES: `admin.name=${digest}`, ADMIN_CONTAINER: { getByName: () => ({ fetch: async () => { calls++; return new Response("ok"); } }) } };
  const request = (token) => new Request("https://admin.example/api/admin/capabilities", { headers: { Authorization: `Bearer ${token}` } });
  assert.equal((await worker.fetch(request("admin.name.wrong"), env)).status, 401);
  assert.equal(calls, 0);
  assert.equal((await worker.fetch(request("admin.name.secret"), env)).status, 200);
  assert.equal(calls, 1);
});

test("durable audit rejects invalid secrets and broken chains", async () => {
  const data = new Map();
  const storage = {
    async list({ prefix, reverse, limit }) { let entries = [...data.entries()].filter(([key]) => key.startsWith(prefix)).sort(([a], [b]) => a.localeCompare(b)); if (reverse) entries.reverse(); return new Map(entries.slice(0, limit)); },
    async get(key) { return data.get(key); },
    async put(values) { for (const [key, value] of Object.entries(values)) data.set(key, value); },
    async delete(key) { data.delete(key); },
  };
  const instance = new AdminContainer({ storage, blockConcurrencyWhile: (fn) => fn() }, { OMNIGHOST_ADMIN_AUDIT_SECRET: "audit-secret" });
  const request = (secret, record) => new Request("https://admin.example/__internal/audit", { method: "POST", headers: { "X-Omni-Audit-Secret": secret }, body: JSON.stringify(record) });
  const first = { previousHash: "0".repeat(64), hash: "a".repeat(64) };
  assert.equal((await instance.audit(request("bad", first))).status, 401);
  assert.equal((await instance.audit(request("audit-secret", first))).status, 204);
  assert.equal((await instance.audit(request("audit-secret", first))).status, 409);
  const response = await instance.audit(new Request("https://admin.example/__internal/audit?limit=1", { headers: { "X-Omni-Audit-Secret": "audit-secret" } }));
  assert.deepEqual(await response.json(), [first]);
});
