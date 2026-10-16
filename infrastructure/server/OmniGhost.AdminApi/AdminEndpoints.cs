using System.Globalization;
using System.Text;
using System.Text.Json.Nodes;

namespace OmniGhost.AdminApi;

public sealed record BanUserRequest(string Reason);
public sealed record GenerateLicenseRequest(int Days, int Level, int Amount, string? Mask, string? Note);
public sealed record LicenseActionRequest(string? Reason, bool UserToo = false);
public sealed record AssignLicenseRequest(string Username);
public sealed record BulkUserActionRequest(IReadOnlyList<string> Usernames, string Action, string? Reason);

public static class AdminEndpoints
{
    private static KeyValuePair<string, string?> Parameter(string name, string? value) => new(name, value);

    public static IEndpointRouteBuilder MapAdminEndpoints(this IEndpointRouteBuilder endpoints)
    {
        var admin = endpoints.MapGroup("/api/admin");

        admin.MapGet("/capabilities", (AdminOptions options) => Results.Ok(new {
            service = "OmniGhost.AdminApi",
            configured = options.IsConfigured,
            keyAuth = new {
                users = new[] { "list", "details", "ban", "reset-hwid", "export" },
                licenses = new[] { "list", "generate", "ban", "unban", "delete", "assign", "export" }
            },
            unavailableWithoutAdditionalServices = new[] {
                "impersonation", "temporary-user-ban", "payments", "revenue", "crash-ingestion",
                "remote-feature-flags", "release-promotion", "support-tickets"
            }
        }));

        admin.MapGet("/users", ListUsersAsync);
        admin.MapGet("/users/export", ExportUsersAsync);
        admin.MapGet("/users/{username}", GetUserAsync);
        admin.MapPost("/users/{username}/ban", BanUserAsync);
        admin.MapPost("/users/{username}/unban", UnbanUserAsync);
        admin.MapPost("/users/{username}/reset-hwid", ResetHwidAsync);
        admin.MapPost("/users/bulk", BulkUsersAsync);

        admin.MapGet("/licenses", ListLicensesAsync);
        admin.MapGet("/licenses/export", ExportLicensesAsync);
        admin.MapPost("/licenses/generate", GenerateLicensesAsync);
        admin.MapPost("/licenses/{key}/ban", BanLicenseAsync);
        admin.MapPost("/licenses/{key}/unban", UnbanLicenseAsync);
        admin.MapDelete("/licenses/{key}", DeleteLicenseAsync);
        admin.MapPost("/licenses/{key}/assign", AssignLicenseAsync);
        admin.MapGet("/analytics/summary", AnalyticsSummaryAsync);

        admin.MapGet("/audit-log", async (HttpContext context, AuditLog audit, int? limit) => {
            var records = await audit.ReadRecentAsync(Math.Clamp(limit ?? 100, 1, 500), context.RequestAborted);
            return Results.Ok(new { items = records, count = records.Count });
        });

        return endpoints;
    }

    private static async Task<IResult> ListUsersAsync(
        HttpContext context, KeyAuthSellerClient keyAuth, string? filter, int? page, int? pageSize)
    {
        var response = await keyAuth.CallAsync("fetchallusers", context.RequestAborted);
        if (!response.Success) return UpstreamFailure(response);
        var users = FilterAndPage(response.Payload["users"] as JsonArray, filter, page, pageSize,
            out var total, RedactUserSecrets);
        return Results.Ok(new { items = users, total, page = NormalizePage(page), pageSize = NormalizePageSize(pageSize) });
    }

    private static async Task<IResult> GetUserAsync(
        HttpContext context, KeyAuthSellerClient keyAuth, string username)
    {
        if (!ValidIdentifier(username, 128)) return Results.BadRequest(new { error = "Invalid username." });
        var response = await keyAuth.CallAsync("userdata", context.RequestAborted, Parameter("user", username));
        if (!response.Success) return UpstreamFailure(response);
        return Results.Ok(RedactUserSecrets(response.Payload));
    }

    private static async Task<IResult> BanUserAsync(
        HttpContext context, KeyAuthSellerClient keyAuth, AuditLog audit,
        string username, BanUserRequest request)
    {
        if (!ValidIdentifier(username, 128) || string.IsNullOrWhiteSpace(request.Reason) || request.Reason.Length > 256)
            return Results.BadRequest(new { error = "Username and a reason up to 256 characters are required." });
        var response = await keyAuth.CallAsync("banuser", context.RequestAborted,
            Parameter("user", username), Parameter("reason", request.Reason.Trim()));
        await audit.WriteAsync(context.Actor(), context, "user.ban", response.Success,
            $"target={username}; result={response.Message}");
        return SellerResult(response);
    }

    private static async Task<IResult> ResetHwidAsync(
        HttpContext context, KeyAuthSellerClient keyAuth, AuditLog audit, string username)
    {
        if (!ValidIdentifier(username, 128)) return Results.BadRequest(new { error = "Invalid username." });
        var response = await keyAuth.CallAsync("resetuser", context.RequestAborted, Parameter("user", username));
        await audit.WriteAsync(context.Actor(), context, "user.reset_hwid", response.Success,
            $"target={username}; result={response.Message}");
        return SellerResult(response);
    }

    private static async Task<IResult> UnbanUserAsync(
        HttpContext context, KeyAuthSellerClient keyAuth, AuditLog audit, string username)
    {
        if (!ValidIdentifier(username, 128)) return Results.BadRequest(new { error = "Invalid username." });
        var assigned = await keyAuth.CallAsync("getkey", context.RequestAborted, Parameter("user", username));
        var key = assigned.Payload["key"]?.ToString();
        if (!assigned.Success || string.IsNullOrWhiteSpace(key)) {
            await audit.WriteAsync(context.Actor(), context, "user.unban", false,
                $"target={username}; result={assigned.Message}");
            return UpstreamFailure(assigned);
        }
        var response = await keyAuth.CallAsync("unban", context.RequestAborted, Parameter("key", key));
        await audit.WriteAsync(context.Actor(), context, "user.unban", response.Success,
            $"target={username}; key={Fingerprint(key)}; result={response.Message}");
        return SellerResult(response);
    }

    private static async Task<IResult> BulkUsersAsync(
        HttpContext context, KeyAuthSellerClient keyAuth, AuditLog audit, BulkUserActionRequest request)
    {
        if (request.Usernames is null || request.Usernames.Count is < 1 or > 50 ||
            request.Usernames.Any(username => !ValidIdentifier(username, 128)))
            return Results.BadRequest(new { error = "Provide between 1 and 50 valid usernames." });
        var action = request.Action.Trim().ToLowerInvariant();
        if (action is not ("ban" or "unban" or "reset-hwid"))
            return Results.BadRequest(new { error = "Supported actions: ban, unban, reset-hwid." });
        if (action == "ban" && (string.IsNullOrWhiteSpace(request.Reason) || request.Reason.Length > 256))
            return Results.BadRequest(new { error = "A reason up to 256 characters is required for a ban." });

        var results = new List<object>(request.Usernames.Count);
        var successful = 0;
        foreach (var username in request.Usernames.Distinct(StringComparer.OrdinalIgnoreCase)) {
            SellerResponse response;
            if (action == "ban") {
                response = await keyAuth.CallAsync("banuser", context.RequestAborted,
                    Parameter("user", username), Parameter("reason", request.Reason!.Trim()));
            } else if (action == "reset-hwid") {
                response = await keyAuth.CallAsync("resetuser", context.RequestAborted, Parameter("user", username));
            } else {
                var assigned = await keyAuth.CallAsync("getkey", context.RequestAborted, Parameter("user", username));
                var key = assigned.Payload["key"]?.ToString();
                response = assigned.Success && !string.IsNullOrWhiteSpace(key)
                    ? await keyAuth.CallAsync("unban", context.RequestAborted, Parameter("key", key))
                    : assigned;
            }
            if (response.Success) ++successful;
            results.Add(new { username, success = response.Success, message = response.Message });
        }
        await audit.WriteAsync(context.Actor(), context, $"user.bulk.{action}", successful == results.Count,
            $"requested={request.Usernames.Count}; successful={successful}");
        return Results.Ok(new { requested = request.Usernames.Count, successful, items = results });
    }

    private static async Task<IResult> ExportUsersAsync(
        HttpContext context, KeyAuthSellerClient keyAuth, AuditLog audit, string? filter)
    {
        var response = await keyAuth.CallAsync("fetchallusers", context.RequestAborted);
        if (!response.Success) return UpstreamFailure(response);
        var users = FilterAll(response.Payload["users"] as JsonArray, filter, RedactUserSecrets, 5000);
        var csv = ToCsv(users, ["username", "email", "ip", "hwid", "createdate", "lastlogin", "banned"]);
        // API-002 Fix: exports contain PII and must be audited like mutations.
        await audit.WriteAsync(context.Actor(), context, "user.export", true,
            $"filter={filter ?? "all"}; rows={users.Count}");
        return Results.File(Encoding.UTF8.GetBytes(csv), "text/csv; charset=utf-8", "omnighost-users.csv");
    }

    private static async Task<IResult> ListLicensesAsync(
        HttpContext context, KeyAuthSellerClient keyAuth, string? filter, int? page, int? pageSize)
    {
        var response = await keyAuth.CallAsync("fetchallkeys", context.RequestAborted, Parameter("format", "json"));
        if (!response.Success) return UpstreamFailure(response);
        var keys = FilterAndPage(response.Payload["keys"] as JsonArray, filter, page, pageSize,
            out var total, node => node.DeepClone());
        return Results.Ok(new { items = keys, total, page = NormalizePage(page), pageSize = NormalizePageSize(pageSize) });
    }

    private static async Task<IResult> GenerateLicensesAsync(
        HttpContext context, KeyAuthSellerClient keyAuth, AuditLog audit, GenerateLicenseRequest request)
    {
        if (request.Days is < 1 or > 3650 || request.Level is < 1 or > 100 || request.Amount is < 1 or > 100 ||
            request.Mask?.Length > 128 || request.Note?.Length > 256)
            return Results.BadRequest(new { error = "Invalid days, level, amount, mask, or note." });
        var response = await keyAuth.CallAsync("add", context.RequestAborted,
            Parameter("format", "JSON"),
            Parameter("expiry", request.Days.ToString(CultureInfo.InvariantCulture)),
            Parameter("level", request.Level.ToString(CultureInfo.InvariantCulture)),
            Parameter("amount", request.Amount.ToString(CultureInfo.InvariantCulture)),
            Parameter("mask", request.Mask?.Trim()),
            Parameter("note", request.Note?.Trim()),
            Parameter("displayToken", "false"));
        await audit.WriteAsync(context.Actor(), context, "license.generate", response.Success,
            $"amount={request.Amount}; days={request.Days}; level={request.Level}; result={response.Message}");
        return SellerResult(response);
    }

    private static async Task<IResult> BanLicenseAsync(
        HttpContext context, KeyAuthSellerClient keyAuth, AuditLog audit, string key, LicenseActionRequest request)
    {
        if (!ValidIdentifier(key, 256) || string.IsNullOrWhiteSpace(request.Reason) || request.Reason.Length > 256)
            return Results.BadRequest(new { error = "License and a reason up to 256 characters are required." });
        var response = await keyAuth.CallAsync("ban", context.RequestAborted,
            Parameter("key", key), Parameter("reason", request.Reason.Trim()),
            Parameter("userToo", request.UserToo ? "true" : "false"));
        await audit.WriteAsync(context.Actor(), context, "license.ban", response.Success,
            $"key={Fingerprint(key)}; result={response.Message}");
        return SellerResult(response);
    }

    private static async Task<IResult> UnbanLicenseAsync(
        HttpContext context, KeyAuthSellerClient keyAuth, AuditLog audit, string key)
    {
        if (!ValidIdentifier(key, 256)) return Results.BadRequest(new { error = "Invalid license." });
        var response = await keyAuth.CallAsync("unban", context.RequestAborted, Parameter("key", key));
        await audit.WriteAsync(context.Actor(), context, "license.unban", response.Success,
            $"key={Fingerprint(key)}; result={response.Message}");
        return SellerResult(response);
    }

    private static async Task<IResult> DeleteLicenseAsync(
        HttpContext context, KeyAuthSellerClient keyAuth, AuditLog audit, string key, bool? userToo)
    {
        if (!ValidIdentifier(key, 256)) return Results.BadRequest(new { error = "Invalid license." });
        var response = await keyAuth.CallAsync("del", context.RequestAborted,
            Parameter("key", key), Parameter("userToo", userToo == true ? "true" : "false"));
        await audit.WriteAsync(context.Actor(), context, "license.delete", response.Success,
            $"key={Fingerprint(key)}; result={response.Message}");
        return SellerResult(response);
    }

    private static async Task<IResult> AssignLicenseAsync(
        HttpContext context, KeyAuthSellerClient keyAuth, AuditLog audit,
        string key, AssignLicenseRequest request)
    {
        if (!ValidIdentifier(key, 256) || !ValidIdentifier(request.Username, 128))
            return Results.BadRequest(new { error = "Invalid license or username." });
        var response = await keyAuth.CallAsync("assignkey", context.RequestAborted,
            Parameter("key", key), Parameter("user", request.Username));
        await audit.WriteAsync(context.Actor(), context, "license.assign", response.Success,
            $"key={Fingerprint(key)}; target={request.Username}; result={response.Message}");
        return SellerResult(response);
    }

    private static async Task<IResult> ExportLicensesAsync(
        HttpContext context, KeyAuthSellerClient keyAuth, AuditLog audit, string? filter)
    {
        var response = await keyAuth.CallAsync("fetchallkeys", context.RequestAborted, Parameter("format", "json"));
        if (!response.Success) return UpstreamFailure(response);
        var keys = FilterAll(response.Payload["keys"] as JsonArray, filter, node => node.DeepClone(), 5000);
        var csv = ToCsv(keys, ["key", "status", "level", "expiry", "owner", "note"]);
        // API-002 Fix: full license keys leave the system here — audit it.
        await audit.WriteAsync(context.Actor(), context, "license.export", true,
            $"filter={filter ?? "all"}; rows={keys.Count}");
        return Results.File(Encoding.UTF8.GetBytes(csv), "text/csv; charset=utf-8", "omnighost-licenses.csv");
    }

    private static async Task<IResult> AnalyticsSummaryAsync(HttpContext context, KeyAuthSellerClient keyAuth)
    {
        var usersTask = keyAuth.CallAsync("fetchallusers", context.RequestAborted);
        var licensesTask = keyAuth.CallAsync("fetchallkeys", context.RequestAborted, Parameter("format", "json"));
        await Task.WhenAll(usersTask, licensesTask);
        var users = await usersTask;
        var licenses = await licensesTask;
        if (!users.Success) return UpstreamFailure(users);
        if (!licenses.Success) return UpstreamFailure(licenses);
        var userItems = users.Payload["users"] as JsonArray ?? [];
        var licenseItems = licenses.Payload["keys"] as JsonArray ?? [];
        var bannedUsers = userItems.OfType<JsonObject>().Count(user => IsTruthy(user["banned"]));
        var usedLicenses = licenseItems.OfType<JsonObject>().Count(key =>
            !string.IsNullOrWhiteSpace(key["usedby"]?.ToString()) || !string.IsNullOrWhiteSpace(key["owner"]?.ToString()));
        return Results.Ok(new {
            generatedAt = DateTimeOffset.UtcNow,
            users = new { total = userItems.Count, banned = bannedUsers },
            licenses = new { total = licenseItems.Count, used = usedLicenses, unused = Math.Max(0, licenseItems.Count - usedLicenses) },
            note = "KeyAuth inventory only; no revenue or private client telemetry is collected."
        });
    }

    private static List<JsonNode> FilterAndPage(
        JsonArray? source, string? filter, int? page, int? pageSize,
        out int total, Func<JsonNode, JsonNode> projection)
    {
        var normalizedFilter = filter?.Trim() ?? string.Empty;
        var filtered = (source ?? []).Where(item => item is not null &&
            (normalizedFilter.Length == 0 || item.ToJsonString().Contains(normalizedFilter, StringComparison.OrdinalIgnoreCase)))
            .Cast<JsonNode>().ToList();
        total = filtered.Count;
        var normalizedPage = NormalizePage(page);
        var normalizedSize = NormalizePageSize(pageSize);
        return filtered.Skip((normalizedPage - 1) * normalizedSize).Take(normalizedSize).Select(projection).ToList();
    }

    private static List<JsonNode> FilterAll(
        JsonArray? source, string? filter, Func<JsonNode, JsonNode> projection, int maximum)
    {
        var normalizedFilter = filter?.Trim() ?? string.Empty;
        return (source ?? []).Where(item => item is not null &&
            (normalizedFilter.Length == 0 || item.ToJsonString().Contains(normalizedFilter, StringComparison.OrdinalIgnoreCase)))
            .Cast<JsonNode>().Take(maximum).Select(projection).ToList();
    }

    private static int NormalizePage(int? value) => Math.Max(value ?? 1, 1);
    private static int NormalizePageSize(int? value) => Math.Clamp(value ?? 50, 1, 200);

    private static JsonNode RedactUserSecrets(JsonNode source)
    {
        var clone = source.DeepClone();
        if (clone is JsonObject value) {
            value.Remove("password");
            value.Remove("token");
        }
        return clone;
    }

    private static IResult SellerResult(SellerResponse response) =>
        response.Success ? Results.Ok(RedactUserSecrets(response.Payload)) : UpstreamFailure(response);

    private static IResult UpstreamFailure(SellerResponse response) =>
        Results.Problem(response.Message, statusCode: StatusCodes.Status502BadGateway);

    private static bool ValidIdentifier(string? value, int maximum) =>
        !string.IsNullOrWhiteSpace(value) && value.Length <= maximum &&
        value.All(character => character >= 0x20 && character != 0x7f);

    private static string Fingerprint(string secret)
    {
        var hash = System.Security.Cryptography.SHA256.HashData(Encoding.UTF8.GetBytes(secret));
        return Convert.ToHexString(hash.AsSpan(0, 6)).ToLowerInvariant();
    }

    private static string ToCsv(IEnumerable<JsonNode> items, IReadOnlyList<string> columns)
    {
        var output = new StringBuilder();
        output.AppendLine(string.Join(',', columns.Select(CsvEscape)));
        foreach (var item in items.OfType<JsonObject>())
            output.AppendLine(string.Join(',', columns.Select(column => CsvEscape(item[column]?.ToString() ?? string.Empty))));
        return output.ToString();
    }

    private static string CsvEscape(string value)
    {
        var safe = value.Length > 0 && "=+-@\t\r".Contains(value[0], StringComparison.Ordinal)
            ? "'" + value : value;
        return '"' + safe.Replace("\"", "\"\"") + '"';
    }

    private static bool IsTruthy(JsonNode? value)
    {
        var text = value?.ToString();
        return string.Equals(text, "true", StringComparison.OrdinalIgnoreCase) || text == "1";
    }
}
