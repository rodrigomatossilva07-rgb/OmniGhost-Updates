using System.Security.Cryptography;
using System.Text;

namespace OmniGhost.AdminApi;

public sealed class ApiKeyAuthenticator(AdminOptions options)
{
    public string? Authenticate(string? authorization)
    {
        const string prefix = "Bearer ";
        if (string.IsNullOrWhiteSpace(authorization) ||
            !authorization.StartsWith(prefix, StringComparison.OrdinalIgnoreCase))
            return null;

        var credential = authorization[prefix.Length..].Trim();
        var separator = credential.IndexOf('.');
        if (separator <= 0 || separator == credential.Length - 1) return null;
        var actor = credential[..separator];
        var secret = credential[(separator + 1)..];
        if (!options.ApiKeyHashes.TryGetValue(actor, out var expectedHex)) return null;

        Span<byte> actual = stackalloc byte[32];
        SHA256.HashData(Encoding.UTF8.GetBytes(secret), actual);
        var expected = Convert.FromHexString(expectedHex);
        return CryptographicOperations.FixedTimeEquals(actual, expected) ? actor : null;
    }
}

public sealed class AdminAuthenticationMiddleware(RequestDelegate next)
{
    public async Task InvokeAsync(
        HttpContext context,
        AdminOptions options,
        ApiKeyAuthenticator apiKeys,
        AdminEntitlementAuthorizer entitlements,
        AuditLog audit)
    {
        if (!context.Request.Path.StartsWithSegments("/api/admin")) {
            await next(context);
            return;
        }

        if (!options.IsConfigured) {
            await audit.WriteAsync("system", context, "auth.configuration", false, "Admin service is not configured.");
            await Results.Problem("Administrative service is not configured.", statusCode: StatusCodes.Status503ServiceUnavailable)
                .ExecuteAsync(context);
            return;
        }

        var actor = apiKeys.Authenticate(context.Request.Headers.Authorization);
        if (actor is null) {
            await audit.WriteAsync("anonymous", context, "auth.api_key", false, "Invalid administrative credential.");
            context.Response.Headers.WWWAuthenticate = "Bearer";
            await Results.Unauthorized().ExecuteAsync(context);
            return;
        }

        var forceRefresh = !HttpMethods.IsGet(context.Request.Method) && !HttpMethods.IsHead(context.Request.Method);
        var authorization = await entitlements.AuthorizeAsync(actor, forceRefresh, context.RequestAborted);
        if (!authorization.Authorized) {
            await audit.WriteAsync(actor, context, "auth.entitlement", false, authorization.Reason);
            await Results.Problem("The account does not have an active Admin level 10 subscription.",
                statusCode: StatusCodes.Status403Forbidden).ExecuteAsync(context);
            return;
        }

        context.Items[AdminContext.ActorKey] = actor;
        await next(context);
    }
}

public static class AdminContext
{
    public const string ActorKey = "OmniGhost.Admin.Actor";
    public static string Actor(this HttpContext context) =>
        context.Items.TryGetValue(ActorKey, out var value) ? value as string ?? "unknown" : "unknown";
}
