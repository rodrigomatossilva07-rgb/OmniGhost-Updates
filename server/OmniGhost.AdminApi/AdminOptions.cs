using System.Globalization;

namespace OmniGhost.AdminApi;

public sealed record AdminOptions(
    Uri KeyAuthSellerEndpoint,
    string KeyAuthSellerKey,
    IReadOnlyDictionary<string, string> ApiKeyHashes,
    string RequiredSubscription,
    int RequiredLevel,
    TimeSpan EntitlementCacheDuration,
    string AuditPath,
    bool TrustForwardedHttps)
{
    public bool IsConfigured =>
        KeyAuthSellerKey.Length >= 16 && ApiKeyHashes.Count > 0;

    public static AdminOptions Load(IConfiguration configuration, IHostEnvironment environment)
    {
        var endpointText = Environment.GetEnvironmentVariable("OMNIGHOST_ADMIN_KEYAUTH_ENDPOINT")
            ?? "https://keyauth.win/api/seller/";
        if (!Uri.TryCreate(endpointText, UriKind.Absolute, out var endpoint) ||
            endpoint.Scheme != Uri.UriSchemeHttps ||
            !string.Equals(endpoint.Host, "keyauth.win", StringComparison.OrdinalIgnoreCase))
            throw new InvalidOperationException("OMNIGHOST_ADMIN_KEYAUTH_ENDPOINT must be https://keyauth.win/.");

        var subscription = configuration["Admin:RequiredSubscription"]?.Trim();
        if (string.IsNullOrWhiteSpace(subscription)) subscription = "Admin";
        var level = configuration.GetValue("Admin:RequiredLevel", 10);
        var cacheSeconds = Math.Clamp(configuration.GetValue("Admin:EntitlementCacheSeconds", 60), 10, 300);
        var auditPath = configuration["Admin:AuditPath"]?.Trim();
        if (string.IsNullOrWhiteSpace(auditPath)) auditPath = "data/audit/admin-audit.jsonl";
        if (!Path.IsPathRooted(auditPath))
            auditPath = Path.GetFullPath(Path.Combine(environment.ContentRootPath, auditPath));

        return new AdminOptions(
            endpoint,
            Environment.GetEnvironmentVariable("OMNIGHOST_ADMIN_KEYAUTH_SELLER_KEY")?.Trim() ?? string.Empty,
            ParseApiKeyHashes(Environment.GetEnvironmentVariable("OMNIGHOST_ADMIN_API_KEY_HASHES")),
            subscription,
            level,
            TimeSpan.FromSeconds(cacheSeconds),
            auditPath,
            string.Equals(Environment.GetEnvironmentVariable("OMNIGHOST_ADMIN_TRUST_FORWARDED_HTTPS"),
                          "true", StringComparison.OrdinalIgnoreCase));
    }

    public static IReadOnlyDictionary<string, string> ParseApiKeyHashes(string? value)
    {
        var result = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
        if (string.IsNullOrWhiteSpace(value)) return result;
        foreach (var entry in value.Split(';', StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries))
        {
            var separator = entry.IndexOf('=');
            if (separator <= 0 || separator == entry.Length - 1) continue;
            var actor = entry[..separator].Trim();
            var hash = entry[(separator + 1)..].Trim().ToLowerInvariant();
            if (!IsActor(actor) || hash.Length != 64 || !hash.All(Uri.IsHexDigit)) continue;
            result[actor] = hash;
        }
        return result;
    }

    private static bool IsActor(string value) =>
        value.Length is >= 1 and <= 64 && value.All(character =>
            char.IsAsciiLetterOrDigit(character) || character is '-' or '_' or '.');
}
