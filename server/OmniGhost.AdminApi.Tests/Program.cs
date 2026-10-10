using System.Security.Cryptography;
using System.Text;
using System.Text.Json.Nodes;
using Microsoft.AspNetCore.Http;
using OmniGhost.AdminApi;

static void Expect(bool condition, string message)
{
    if (!condition) throw new InvalidOperationException(message);
}

var secret = "correct-horse-battery-staple-admin";
var hash = Convert.ToHexString(SHA256.HashData(Encoding.UTF8.GetBytes(secret))).ToLowerInvariant();
var parsed = AdminOptions.ParseApiKeyHashes($"rodrigo={hash};invalid=no;bad actor={hash}");
Expect(parsed.Count == 1 && parsed.ContainsKey("rodrigo"), "API key hash parsing failed.");

var auditPath = Path.Combine(Path.GetTempPath(), "omnighost-admin-tests", Guid.NewGuid() + ".jsonl");
var options = new AdminOptions(
    new Uri("https://keyauth.win/api/seller/"),
    "configured-seller-key-value",
    parsed,
    "Admin",
    10,
    TimeSpan.FromSeconds(60),
    auditPath,
    false);
var authenticator = new ApiKeyAuthenticator(options);
Expect(authenticator.Authenticate($"Bearer rodrigo.{secret}") == "rodrigo", "Valid API key was rejected.");
Expect(authenticator.Authenticate("Bearer rodrigo.wrong") is null, "Invalid API key was accepted.");
Expect(authenticator.Authenticate($"Bearer other.{secret}") is null, "Unknown actor was accepted.");

using var seller = new KeyAuthSellerClient(options);
var authorizer = new AdminEntitlementAuthorizer(options, seller);
var active = new JsonArray(new JsonObject {
    ["subscription"] = "Admin",
    ["level"] = "10",
    ["expiry"] = DateTimeOffset.UtcNow.AddHours(1).ToUnixTimeSeconds().ToString()
});
Expect(authorizer.EvaluateSubscriptions(active, DateTimeOffset.UtcNow).Authorized,
    "Active Admin level 10 subscription was rejected.");
var lowLevel = new JsonArray(new JsonObject {
    ["subscription"] = "Admin",
    ["level"] = "9",
    ["expiry"] = DateTimeOffset.UtcNow.AddHours(1).ToUnixTimeSeconds().ToString()
});
Expect(!authorizer.EvaluateSubscriptions(lowLevel, DateTimeOffset.UtcNow).Authorized,
    "Admin below level 10 was accepted.");
var expired = new JsonArray(new JsonObject {
    ["subscription"] = "Admin",
    ["level"] = "10",
    ["expiry"] = DateTimeOffset.UtcNow.AddSeconds(-1).ToUnixTimeSeconds().ToString()
});
Expect(!authorizer.EvaluateSubscriptions(expired, DateTimeOffset.UtcNow).Authorized,
    "Expired Admin subscription was accepted.");

var audit = new AuditLog(options);
var context = new DefaultHttpContext();
context.TraceIdentifier = "test-request";
context.Request.Method = "POST";
context.Request.Path = "/api/admin/test";
await audit.WriteAsync("rodrigo", context, "test.action", true, "safe detail");
await audit.WriteAsync("rodrigo", context, "test.action.two", false, "safe detail two");
var records = await audit.ReadRecentAsync(10, CancellationToken.None);
Expect(records.Count == 2, "Audit records were not persisted.");
Expect(records[1].PreviousHash == records[0].Hash, "Audit hash chain is broken.");

try { File.Delete(auditPath); } catch (IOException) { }
Console.WriteLine("OmniGhost.AdminApi security tests passed.");
