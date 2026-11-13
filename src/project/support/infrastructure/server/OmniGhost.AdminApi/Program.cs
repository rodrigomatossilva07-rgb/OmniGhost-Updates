using System.Threading.RateLimiting;
using OmniGhost.AdminApi;

var builder = WebApplication.CreateSlimBuilder(args);
builder.WebHost.ConfigureKestrel(options => {
    options.Limits.MaxRequestBodySize = 128 * 1024;
    options.Limits.RequestHeadersTimeout = TimeSpan.FromSeconds(10);
    options.Limits.KeepAliveTimeout = TimeSpan.FromSeconds(30);
});

var adminOptions = AdminOptions.Load(builder.Configuration, builder.Environment);
builder.Services.AddSingleton(adminOptions);
builder.Services.AddSingleton<ApiKeyAuthenticator>();
builder.Services.AddSingleton<KeyAuthSellerClient>();
builder.Services.AddSingleton<AdminEntitlementAuthorizer>();
builder.Services.AddSingleton<AuditLog>();
builder.Services.AddRateLimiter(configuration => {
    configuration.RejectionStatusCode = StatusCodes.Status429TooManyRequests;
    configuration.GlobalLimiter = PartitionedRateLimiter.Create<HttpContext, string>(context =>
        RateLimitPartition.GetFixedWindowLimiter(
            context.Connection.RemoteIpAddress?.ToString() ?? "unknown",
            _ => new FixedWindowRateLimiterOptions {
                AutoReplenishment = true,
                PermitLimit = 120,
                QueueLimit = 0,
                Window = TimeSpan.FromMinutes(1)
            }));
});

var app = builder.Build();
if (!app.Environment.IsDevelopment()) {
    app.UseHsts();
    if (adminOptions.TrustForwardedHttps) {




        app.Use(async (context, next) => {
            var remote = context.Connection.RemoteIpAddress;
            var proto = context.Request.Headers["X-Forwarded-Proto"].ToString();
            if (remote is not null &&
                adminOptions.TrustedForwardedProxies.Any(proxy => proxy.Equals(remote)) &&
                string.Equals(proto, "https", StringComparison.OrdinalIgnoreCase)) {
                context.Request.Scheme = "https";
            }
            if (!context.Request.IsHttps) {
                await Results.Problem("HTTPS is required.", statusCode: StatusCodes.Status426UpgradeRequired)
                    .ExecuteAsync(context);
                return;
            }
            await next();
        });
    } else {
        app.Use(async (context, next) => {
            if (!context.Request.IsHttps) {
                await Results.Problem("HTTPS is required.", statusCode: StatusCodes.Status426UpgradeRequired)
                    .ExecuteAsync(context);
                return;
            }
            await next();
        });
    }
}

app.Use(async (context, next) => {
    context.Response.Headers.XContentTypeOptions = "nosniff";
    context.Response.Headers.XFrameOptions = "DENY";
    context.Response.Headers["Referrer-Policy"] = "no-referrer";
    context.Response.Headers.CacheControl = "no-store";
    context.Response.Headers.ContentSecurityPolicy = "default-src 'none'; frame-ancestors 'none'";
    await next();
});
app.UseRateLimiter();
app.UseMiddleware<AdminAuthenticationMiddleware>();

app.MapGet("/healthz", () => Results.Ok(new { status = "healthy" }));
app.MapGet("/readyz", (AdminOptions options) => options.IsConfigured
    ? Results.Ok(new { status = "ready" })
    : Results.Json(new { status = "not-configured" }, statusCode: StatusCodes.Status503ServiceUnavailable));
app.MapAdminEndpoints();

app.Run();

public partial class Program { }
