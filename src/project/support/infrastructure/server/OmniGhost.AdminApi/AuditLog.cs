using System.Security.Cryptography;
using System.Text;
using System.Text.Json;

namespace OmniGhost.AdminApi;

public sealed record AuditRecord(
    DateTimeOffset Timestamp,
    string RequestId,
    string Actor,
    string Method,
    string Path,
    string Action,
    bool Success,
    string Detail,
    string PreviousHash,
    string Hash);

public sealed class AuditLog
{
    private const long MaximumAuditBytes = 25L * 1024L * 1024L;
    private const int MaximumArchives = 5;
    private readonly AdminOptions options;
    private readonly SemaphoreSlim gate = new(1, 1);
    private string previousHash = new('0', 64);

    public AuditLog(AdminOptions options)
    {
        this.options = options;
        var directory = Path.GetDirectoryName(options.AuditPath);
        if (!string.IsNullOrWhiteSpace(directory)) Directory.CreateDirectory(directory);
        previousHash = ReadLastHash(options.AuditPath) ?? previousHash;
    }

    public async Task WriteAsync(string actor, HttpContext context, string action, bool success, string detail)
    {
        await gate.WaitAsync();
        try {
            RotateIfRequired();
            var timestamp = DateTimeOffset.UtcNow;
            var safeDetail = Sanitize(detail, 512);
            var material = string.Join('|', timestamp.ToString("O"), context.TraceIdentifier, actor,
                context.Request.Method, context.Request.Path.Value ?? string.Empty, action, success, safeDetail, previousHash);
            var hash = Convert.ToHexString(SHA256.HashData(Encoding.UTF8.GetBytes(material))).ToLowerInvariant();
            var record = new AuditRecord(timestamp, context.TraceIdentifier, actor,
                context.Request.Method, context.Request.Path.Value ?? string.Empty, action, success,
                safeDetail, previousHash, hash);
            await File.AppendAllTextAsync(options.AuditPath, JsonSerializer.Serialize(record) + Environment.NewLine,
                Encoding.UTF8, CancellationToken.None);
            previousHash = hash;
        } finally {
            gate.Release();
        }
    }

    public async Task<IReadOnlyList<AuditRecord>> ReadRecentAsync(int maximum, CancellationToken cancellationToken)
    {
        if (!File.Exists(options.AuditPath)) return [];
        var lines = await File.ReadAllLinesAsync(options.AuditPath, cancellationToken);
        var records = new List<AuditRecord>();
        foreach (var line in lines.TakeLast(Math.Clamp(maximum, 1, 1000))) {
            try {
                var record = JsonSerializer.Deserialize<AuditRecord>(line);
                if (record is not null) records.Add(record);
            } catch (JsonException) { }
        }
        return records;
    }

    private static string? ReadLastHash(string path)
    {
        if (!File.Exists(path)) return null;
        try {
            var line = File.ReadLines(path).LastOrDefault(value => !string.IsNullOrWhiteSpace(value));
            return line is null ? null : JsonSerializer.Deserialize<AuditRecord>(line)?.Hash;
        } catch (IOException) { return null; }
          catch (JsonException) { return null; }
    }

    private void RotateIfRequired()
    {
        var file = new FileInfo(options.AuditPath);
        if (!file.Exists || file.Length < MaximumAuditBytes) return;
        for (var index = MaximumArchives - 1; index >= 1; --index) {
            var source = $"{options.AuditPath}.{index}";
            var destination = $"{options.AuditPath}.{index + 1}";
            if (File.Exists(source)) File.Move(source, destination, true);
        }
        File.Move(options.AuditPath, $"{options.AuditPath}.1", true);
    }

    private static string Sanitize(string value, int maximum) {
        var clean = new string(value.Where(character => !char.IsControl(character) || character == ' ').ToArray());
        return clean.Length <= maximum ? clean : clean[..maximum];
    }
}
