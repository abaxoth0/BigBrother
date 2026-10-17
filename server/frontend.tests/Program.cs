using System.Diagnostics;
using System.IO.Pipes;
using System.Text;
using frontend.Services;

static void Check(bool condition, string message)
{
    if (!condition) throw new Exception(message);
}

static async Task Reply(string name, string response, bool expectedRunning = false)
{
    using var pipe = new NamedPipeServerStream(name, PipeDirection.InOut, 1,
        PipeTransmissionMode.Byte, PipeOptions.Asynchronous);
    using var timeout = new CancellationTokenSource(TimeSpan.FromSeconds(5));
    using var ipc = new IpcService(name);
    var request = ipc.GetServerStatusAsync();
    await pipe.WaitForConnectionAsync(timeout.Token);
    using var reader = new StreamReader(pipe, leaveOpen: true);
    Check(await reader.ReadLineAsync(timeout.Token) == "GET_SERVER_STATUS", "Unexpected command");
    Check(await reader.ReadLineAsync(timeout.Token) == "", "Missing request terminator");
    await pipe.WriteAsync(Encoding.UTF8.GetBytes(response), timeout.Token);
    await pipe.FlushAsync(timeout.Token);
    var result = await request;
    Check(result.IsRunning == expectedRunning, "Incorrect backend availability");
    if (expectedRunning)
    {
        Check(result.ConnectedClients == 2 && result.PendingCount == 1, "Incorrect client counts");
        Check(result.FiltrationEnabled && result.UptimeText == "1h 2m", "Incorrect status fields");
    }
}

var name = "BigBrother.Connectivity.Test." + Guid.NewGuid().ToString("N");
using var ipc = new IpcService(name);
Check(!(await ipc.GetServerStatusAsync()).IsRunning, "Missing pipe appeared connected");
const string value = "uptime:1h 2m:clients:2:pending:1:filtration:1";
var validReply = $"OK\n{Encoding.UTF8.GetByteCount(value)}\n{value}\n\n";
await Reply(name, validReply, true);
await Reply(name, "ERROR\n6\ndenied\n\n");
await Reply(name, "OK\n99\ninvalid\n\n");
await Reply(name, "OK\n7\ninvalid\n\n");
await Reply(name, $"OK\n{value.Length}\n{value}\nextra\n");

using (var pipe = new NamedPipeServerStream(name, PipeDirection.InOut, 1,
    PipeTransmissionMode.Byte, PipeOptions.Asynchronous))
{
    var watch = Stopwatch.StartNew();
    var request = ipc.GetServerStatusAsync();
    using var timeout = new CancellationTokenSource(TimeSpan.FromSeconds(5));
    await pipe.WaitForConnectionAsync(timeout.Token);
    Check(!(await request).IsRunning, "Stalled pipe appeared connected");
    Check(watch.Elapsed < TimeSpan.FromSeconds(4), "Status request did not time out");
}
await Reply(name, validReply, true);
Check(!(await ipc.GetServerStatusAsync()).IsRunning, "Disconnected pipe appeared connected");
var fingerprint = new string('A', 64);
foreach (var (reply, expected) in new[]
{
    ($"OK\n64\n{fingerprint}\n\n", fingerprint),
    ("OK\n64\ninvalid\n\n", ""),
    ($"OK\n64\n{new string('G', 64)}\n\n", ""),
})
{
    using var pipe = new NamedPipeServerStream(name, PipeDirection.InOut, 1,
        PipeTransmissionMode.Byte, PipeOptions.Asynchronous);
    using var timeout = new CancellationTokenSource(TimeSpan.FromSeconds(5));
    var request = ipc.GetCertificateFingerprintAsync();
    await pipe.WaitForConnectionAsync(timeout.Token);
    using var reader = new StreamReader(pipe, leaveOpen: true);
    Check(await reader.ReadLineAsync(timeout.Token) == "GET_CERTIFICATE_FINGERPRINT", "Unexpected fingerprint command");
    Check(await reader.ReadLineAsync(timeout.Token) == "", "Missing request terminator");
    await pipe.WriteAsync(Encoding.UTF8.GetBytes(reply), timeout.Token);
    await pipe.FlushAsync(timeout.Token);
    Check(await request == expected, "Invalid fingerprint reply accepted");
}
static async Task<T> WhitelistReply<T>(string name, Func<IpcService, Task<T>> action,
    string[] expectedRequest, string[] data, string status = "OK")
{
    using var pipe = new NamedPipeServerStream(name, PipeDirection.InOut, 1,
        PipeTransmissionMode.Byte, PipeOptions.Asynchronous);
    using var timeout = new CancellationTokenSource(TimeSpan.FromSeconds(5));
    using var service = new IpcService(name);
    var request = Task.Run(() => action(service));
    await pipe.WaitForConnectionAsync(timeout.Token);
    using var reader = new StreamReader(pipe, leaveOpen: true);
    foreach (var line in expectedRequest)
        Check(await reader.ReadLineAsync(timeout.Token) == line, "Unexpected whitelist request");
    Check(await reader.ReadLineAsync(timeout.Token) == "", "Missing whitelist request terminator");
    var response = status + "\n" + string.Concat(data.Select(v => $"{Encoding.UTF8.GetByteCount(v)}\n{v}\n")) + "\n";
    await pipe.WriteAsync(Encoding.UTF8.GetBytes(response), timeout.Token);
    await pipe.FlushAsync(timeout.Token);
    return await request.WaitAsync(timeout.Token);
}

var whitelists = await WhitelistReply(name, service => service.GetWhitelistsAsync(),
    ["GET_WHITELISTS"], ["Работа:основной:3:true", "Второй:2:false"]);
Check(whitelists.Count == 2 && whitelists[0].Name == "Работа:основной" &&
    whitelists[0].EntryCount == 3 && whitelists[0].IsActive && !whitelists[1].IsActive,
    "Whitelist names, counts or active flags lost");

var conflicts = await WhitelistReply(name, service => service.GetWhitelistConflictsAsync(),
    ["GET_WHITELIST_CONFLICTS"],
    ["[{\"pattern\":\"google.com\",\"allowedBy\":[\"Работа\",\"Учёба\"],\"excludedBy\":[\"Исключения\"]}]"]);
Check(conflicts.Count == 1 && conflicts[0].AllowedRule == "google.com" &&
    conflicts[0].ExcludedRule == "!google.com" && conflicts[0].AllowedSources == "Работа, Учёба" &&
    conflicts[0].ExcludedSources == "Исключения", "Conflict details lost");
Check((await WhitelistReply(name, service => service.GetWhitelistConflictsAsync(),
    ["GET_WHITELIST_CONFLICTS"], ["[]"])).Count == 0, "Resolved conflicts retained");

foreach (var active in new[] { true, false })
{
    var states = new Dictionary<string, bool> { ["Работа"] = active, ["Учёба"] = !active };
    Check(await WhitelistReply(name, service => service.SetWhitelistsActiveAsync(states),
        ["SET_WHITELISTS_ACTIVE", "12", "Работа", "1", active ? "1" : "0", "10", "Учёба", "1", active ? "0" : "1"], []),
        "Batch activation request failed");
}
Check(!await WhitelistReply(name, service => service.SetWhitelistsActiveAsync(new Dictionary<string, bool> { ["Работа"] = true }),
    ["SET_WHITELISTS_ACTIVE", "12", "Работа", "1", "1"], ["save failed"], "ERROR"),
    "Activation failure ignored");
try
{
    await WhitelistReply(name, service => service.GetWhitelistsAsync(), ["GET_WHITELISTS"], ["load failed"], "ERROR");
    throw new Exception("Failed whitelist load accepted as empty list");
}
catch (IOException) { }
var composed = await WhitelistReply(name, service => service.GetComposedWhitelistAsync(),
    ["GET_COMPOSED_WHITELIST_DETAILS"],
    ["[{\"ID\":\"a\",\"Value\":\"*.example.com\",\"sources\":[\"Работа\",\"Учёба\"]},{\"ID\":\"b\",\"Value\":\"!blocked.example.com\",\"sources\":[\"Исключения\"]}]"]);
Check(composed.Select(r => r.Value).SequenceEqual(new[] { "*.example.com", "!blocked.example.com" }), "Composed rules lost");
Check(composed[0].Sources.SequenceEqual(new[] { "Работа", "Учёба" }) &&
    composed[1].Sources.SequenceEqual(new[] { "Исключения" }), "Composed rule sources lost");
Check((await WhitelistReply(name, service => service.GetComposedWhitelistAsync(),
    ["GET_COMPOSED_WHITELIST_DETAILS"], ["[]"])).Count == 0, "Empty composition rejected");
try
{
    await WhitelistReply(name, service => service.GetComposedWhitelistAsync(),
        ["GET_COMPOSED_WHITELIST_DETAILS"], ["load failed"], "ERROR");
    throw new Exception("Composition load failure ignored");
}
catch (IOException) { }
Console.WriteLine("Connectivity, certificate fingerprint and whitelist pipe tests passed.");
