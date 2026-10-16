using System.Net;
using System.Text.Json.Nodes;

namespace OmniGhost.AdminApi;

public sealed record SellerResponse(bool Success, string Message, JsonObject Payload);

public sealed class KeyAuthSellerClient : IDisposable
{
    private const int MaximumResponseBytes = 8 * 1024 * 1024;
    private readonly AdminOptions options;
    private readonly HttpClient http;

    public KeyAuthSellerClient(AdminOptions options)
    {
        this.options = options;
        var handler = new SocketsHttpHandler {
            AllowAutoRedirect = false,
            AutomaticDecompression = DecompressionMethods.GZip | DecompressionMethods.Deflate,
            ConnectTimeout = TimeSpan.FromSeconds(8),
            PooledConnectionLifetime = TimeSpan.FromMinutes(5)
        };
        http = new HttpClient(handler) { Timeout = TimeSpan.FromSeconds(15) };
        http.DefaultRequestHeaders.UserAgent.ParseAdd("OmniGhost-AdminApi/1.0");
    }

    public async Task<SellerResponse> CallAsync(
        string type,
        IEnumerable<KeyValuePair<string, string?>> parameters,
        CancellationToken cancellationToken)
    {
        if (!options.IsConfigured)
            return new(false, "KeyAuth Seller API is not configured.", new JsonObject());

        // API-001 Fix: the KeyAuth Seller API only accepts the seller key as a
        // GET query parameter, so it WILL appear in URLs. Move the credentials
        // into the POST body instead (KeyAuth also accepts form-encoded POST
        // for seller calls), keeping secrets out of query strings, proxies and
        // access logs. The response handling below is unchanged.
        var content = new List<KeyValuePair<string, string?>> {
            new("sellerkey", options.KeyAuthSellerKey),
            new("type", type)
        };
        content.AddRange(parameters);
        var uri = options.KeyAuthSellerEndpoint;

        try {
            using var body = new FormUrlEncodedContent(
                content.Where(item => item.Value is not null)
                       .Select(item => new KeyValuePair<string, string>(item.Key, item.Value!)));
            using var request = new HttpRequestMessage(HttpMethod.Post, uri) { Content = body };
            using var response = await http.SendAsync(request, HttpCompletionOption.ResponseHeadersRead, cancellationToken);
            if ((int)response.StatusCode is < 200 or >= 300)
                return new(false, $"KeyAuth returned HTTP {(int)response.StatusCode}.", new JsonObject());
            if (response.Content.Headers.ContentLength is > MaximumResponseBytes)
                return new(false, "KeyAuth response exceeded the configured limit.", new JsonObject());
            await using var source = await response.Content.ReadAsStreamAsync(cancellationToken);
            using var buffer = new MemoryStream();
            var chunk = new byte[81920];
            int read;
            while ((read = await source.ReadAsync(chunk, cancellationToken)) > 0) {
                if (buffer.Length + read > MaximumResponseBytes)
                    return new(false, "KeyAuth response exceeded the configured limit.", new JsonObject());
                await buffer.WriteAsync(chunk.AsMemory(0, read), cancellationToken);
            }
            buffer.Position = 0;
            var node = await JsonNode.ParseAsync(buffer, cancellationToken: cancellationToken) as JsonObject;
            if (node is null) return new(false, "KeyAuth returned an invalid JSON object.", new JsonObject());
            var success = node["success"]?.GetValue<bool?>() == true;
            var message = node["message"]?.GetValue<string>() ?? (success ? "OK" : "KeyAuth rejected the request.");
            return new(success, message, node);
        } catch (OperationCanceledException) when (!cancellationToken.IsCancellationRequested) {
            return new(false, "KeyAuth request timed out.", new JsonObject());
        } catch (HttpRequestException) {
            return new(false, "KeyAuth is temporarily unavailable.", new JsonObject());
        } catch (System.Text.Json.JsonException) {
            return new(false, "KeyAuth returned malformed JSON.", new JsonObject());
        }
    }

    public Task<SellerResponse> CallAsync(string type, CancellationToken cancellationToken,
        params KeyValuePair<string, string?>[] parameters) => CallAsync(type, parameters, cancellationToken);

    public void Dispose() => http.Dispose();
}
