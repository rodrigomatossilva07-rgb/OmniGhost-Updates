using System.Collections.Concurrent;
using System.Globalization;
using System.Text.Json.Nodes;

namespace OmniGhost.AdminApi;

public sealed record AuthorizationResult(bool Authorized, string Reason);

public sealed class AdminEntitlementAuthorizer(AdminOptions options, KeyAuthSellerClient keyAuth)
{
    private sealed record CacheEntry(DateTimeOffset ValidUntil, AuthorizationResult Result);
    private readonly ConcurrentDictionary<string, CacheEntry> cache = new(StringComparer.OrdinalIgnoreCase);

    public async Task<AuthorizationResult> AuthorizeAsync(
        string actor,
        bool forceRefresh,
        CancellationToken cancellationToken)
    {
        var now = DateTimeOffset.UtcNow;
        if (!forceRefresh && cache.TryGetValue(actor, out var cached) && cached.ValidUntil > now)
            return cached.Result;

        var response = await keyAuth.CallAsync("userdata", cancellationToken,
            new KeyValuePair<string, string?>("user", actor));
        var result = response.Success
            ? EvaluateSubscriptions(response.Payload["subscriptions"] as JsonArray, now)
            : new AuthorizationResult(false, response.Message);
        cache[actor] = new CacheEntry(now + options.EntitlementCacheDuration, result);
        return result;
    }

    public AuthorizationResult EvaluateSubscriptions(JsonArray? subscriptions, DateTimeOffset now)
    {
        if (subscriptions is null) return new(false, "KeyAuth did not return subscriptions.");
        foreach (var entry in subscriptions.OfType<JsonObject>()) {
            var name = ReadString(entry, "subscription") ?? ReadString(entry, "name");
            if (!string.Equals(name, options.RequiredSubscription, StringComparison.OrdinalIgnoreCase)) continue;
            var levelText = ReadString(entry, "level");
            if (!int.TryParse(levelText, NumberStyles.Integer, CultureInfo.InvariantCulture, out var level) ||
                level < options.RequiredLevel)
                return new(false, "Admin subscription level is below the required level.");
            var expiryText = ReadString(entry, "expiry") ?? ReadString(entry, "expiresAt");
            if (!long.TryParse(expiryText, NumberStyles.Integer, CultureInfo.InvariantCulture, out var expiry) ||
                expiry <= now.ToUnixTimeSeconds())
                return new(false, "Admin subscription is expired or has no verifiable expiry.");
            return new(true, "Authorized.");
        }
        return new(false, "Admin subscription was not found.");
    }

    private static string? ReadString(JsonObject value, string name) {
        var node = value[name];
        if (node is null) return null;
        return node.ToString().Trim('"');
    }
}
