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
    string[] expectedRequest, string[] data, string status = "OK",
    (string[] Request, string[] Data, string Status)[]? followupReplies = null)
{
    using var pipe = new NamedPipeServerStream(name, PipeDirection.InOut, 1,
        PipeTransmissionMode.Byte, PipeOptions.Asynchronous);
    using var timeout = new CancellationTokenSource(TimeSpan.FromSeconds(5));
    using var service = new IpcService(name);
    var request = Task.Run(() => action(service));
    await pipe.WaitForConnectionAsync(timeout.Token);
    using var reader = new StreamReader(pipe, leaveOpen: true);
    async Task SendReply(string[] expected, string[] values, string replyStatus)
    {
        foreach (var line in expected)
            Check(await reader.ReadLineAsync(timeout.Token) == line, "Unexpected whitelist request");
        Check(await reader.ReadLineAsync(timeout.Token) == "", "Missing whitelist request terminator");
        var response = replyStatus + "\n" + string.Concat(values.Select(v => $"{Encoding.UTF8.GetByteCount(v)}\n{v}\n")) + "\n";
        await pipe.WriteAsync(Encoding.UTF8.GetBytes(response), timeout.Token);
        await pipe.FlushAsync(timeout.Token);
    }
    await SendReply(expectedRequest, data, status);
    foreach (var reply in followupReplies ?? [])
        await SendReply(reply.Request, reply.Data, reply.Status);
    return await request.WaitAsync(timeout.Token);
}

var whitelists = await WhitelistReply(name, service => service.GetWhitelistsAsync(),
    ["GET_WHITELISTS_DETAILS"], ["""[{"Name":"Работа:основной","IsActive":true},{"Name":"Второй","IsActive":false}]"""], followupReplies:
    [
        (["GET_WHITELIST", Encoding.UTF8.GetByteCount("Работа:основной").ToString(), "Работа:основной"],
            ["; test", "; comment", "portquiz.net:{8000-9000}", "portquiz.net:{2000-3000|5432}", "!portquiz.net:50"], "OK"),
        (["GET_WHITELIST", "12", "Второй"], ["  # comment", "\t; comment"], "OK")
    ]);
Check(whitelists.Count == 2 && whitelists[0].Name == "Работа:основной" &&
    whitelists[0].EntryCount == 3 && whitelists[1].EntryCount == 0 &&
    whitelists[0].IsActive && !whitelists[1].IsActive,
    "Whitelist table must count parsed rules instead of the backend's raw entry totals");
Check((await WhitelistReply(name, service => service.GetWhitelistsAsync(),
    ["GET_WHITELISTS_DETAILS"], ["""[{"Name":"empty","IsActive":false}]"""], followupReplies:
    [(["GET_WHITELIST", "5", "empty"], [], "OK")])).Single().EntryCount == 0,
    "Empty whitelist must show zero rules");
try
{
    await WhitelistReply(name, service => service.GetWhitelistsAsync(),
        ["GET_WHITELISTS_DETAILS"], ["""[{"Name":"ports","IsActive":false}]"""], followupReplies:
        [(["GET_WHITELIST", "5", "ports"], ["load failed"], "ERROR")]);
    throw new Exception("Failed entry load accepted as a zero rule count");
}
catch (IOException) { }

var defaultPriorityRows = await WhitelistReply(name, service => service.GetWhitelistsAsync(),
    ["GET_WHITELISTS_DETAILS"], ["""[{"Name":"priority","IsActive":true}]"""],
    followupReplies: [(["GET_WHITELIST", "8", "priority"], ["# comment", "[10]", "github.com", "[0]!gist.github.com"], "OK")]);
Check(defaultPriorityRows.Single().EntryCount == 2, "Default priority declaration must not count as a whitelist rule");
var temporaryRows = await WhitelistReply(name, service => service.GetWhitelistsAsync(),
    ["GET_WHITELISTS_DETAILS"],
    ["""[{"Name":"temporary","IsActive":true,"temporary":{"durationSeconds":600,"remainingSeconds":1200,"autoDelete":true}}]"""],
    followupReplies: [(["GET_WHITELIST", "9", "temporary"], ["example.com"], "OK")]);
Check(temporaryRows.Single() is { IsTemporary: true, TimerIsRunning: true, Temporary.DurationSeconds: 600, Temporary.AutoDelete: true } &&
    temporaryRows[0].RemainingSeconds > 1195 && temporaryRows[0].RemainingSeconds <= 1200,
    "Temporary timer metadata or countdown above initial duration lost");
foreach (var command in new[] {
    (Name: "CREATE_TEMPORARY_WHITELIST", Args: new[] { "temporary", "900", "0" }),
    (Name: "UPDATE_WHITELIST_TIMER", Args: new[] { "temporary", "1200", "unchanged", "1" }),
    (Name: "UPDATE_WHITELIST_TIMER", Args: new[] { "temporary", "600", "1200", "0" }),
    (Name: "RESET_WHITELIST_TIMERS", Args: new[] { "temporary" }),
    (Name: "SET_SAVE_TEMPORARY_WHITELIST_STATE", Args: new[] { "1" })
})
{
    var expected = new[] { command.Name }.Concat(command.Args.SelectMany(arg => new[] { Encoding.UTF8.GetByteCount(arg).ToString(), arg })).ToArray();
    await WhitelistReply(name, service => service.TemporaryWhitelistCommandAsync(command.Name, command.Args), expected, []);
}
Check((await WhitelistReply(name, service => service.TemporaryWhitelistCommandAsync("GET_SAVE_TEMPORARY_WHITELIST_STATE"),
    ["GET_SAVE_TEMPORARY_WHITELIST_STATE"], ["1"])).Single() == "1", "Timer persistence preference lost");
try
{
    await WhitelistReply(name, service => service.TemporaryWhitelistCommandAsync("RESET_WHITELIST_TIMERS", "gone"),
        ["RESET_WHITELIST_TIMERS", "4", "gone"], ["missing whitelist"], "ERROR");
    throw new Exception("Timer command failure ignored");
}
catch (IOException ex) { Check(ex.Message == "missing whitelist", "Timer command error details lost"); }

Check(await WhitelistReply(name, service => service.SyncWhitelistsAsync(), ["SYNC_WHITELISTS"], []),
    "Whitelist synchronization request failed");
Check(!await WhitelistReply(name, service => service.SyncWhitelistsAsync(), ["SYNC_WHITELISTS"], ["load failed"], "ERROR"),
    "Whitelist synchronization failure ignored");

var clientDetails = "[{\"name\":\"Клиент:1\",\"address\":\"[::1]:1984\",\"lastActivity\":\"2026-10-22T09:10:11Z\"," +
    "\"policy\":{\"whitelistRevision\":\"revision\",\"filtrationEnabled\":true,\"blockIpv6\":false,\"canSetIpv6Block\":true},\"whitelistCurrent\":true}," +
    "{\"name\":\"old\",\"address\":\"10.0.0.2\",\"policy\":null,\"whitelistCurrent\":null}]";
var clients = await WhitelistReply(name, service => service.GetClientsAsync(),
    ["GET_CLIENTS_DETAILS"], [clientDetails]);
Check(clients.Count == 2 && clients[0].Name == "Клиент:1" && clients[0].Address == "[::1]:1984",
    "Client names or IPv6 addresses lost");
var registeredUsers = await WhitelistReply(name, service => service.GetRegisteredUsersAsync(),
    ["GET_REGISTERED_USERS"], ["[{\"name\":\"offline:клиент\",\"address\":\"[2001:db8::2]:1984\"}]"]);
Check(registeredUsers.Count == 1 && registeredUsers[0].Name == "offline:клиент" &&
    registeredUsers[0].Address == "[2001:db8::2]:1984" && registeredUsers[0].Policy == null && registeredUsers[0].LastActivity == null,
    "Registry fields or unknown offline policy/activity were corrupted");
Check((await WhitelistReply(name, service => service.GetRegisteredUsersAsync(), ["GET_REGISTERED_USERS"], ["[]"])).Count == 0,
    "Empty registry did not remain empty");
try
{
    await WhitelistReply(name, service => service.GetRegisteredUsersAsync(), ["GET_REGISTERED_USERS"], ["database error"], "ERROR");
    throw new Exception("Failed registry query returned an empty successful list");
}
catch (IOException) { }
Check(clients[0].Whitelist == "Актуален" && clients[0].FiltrationText == "Вкл" && clients[0].Ipv6BlockText == "Выкл",
    "Incorrect client policy labels");
Check(clients[0].Policy?.CanSetIpv6Block == true, "IPv6 control capability lost");
Check(clients[0].LastActivity == new DateTime(2026, 10, 22, 9, 10, 11, DateTimeKind.Utc).ToLocalTime(),
    "Last activity was not converted to local time");
Check(clients[1].LastActivity == null && clients[1].Whitelist == "Нет данных" && clients[1].FiltrationText == "Нет данных",
    "Older client displayed invented policy or activity");
var changedProperties = new List<string?>();
clients[1].PropertyChanged += (_, e) => changedProperties.Add(e.PropertyName);
clients[1].Policy = clients[0].Policy;
clients[1].WhitelistCurrent = false;
clients[1].LastActivity = clients[0].LastActivity;
Check(clients[1].Whitelist == "Устарел" && changedProperties.Contains("Whitelist") &&
    changedProperties.Contains("FiltrationText") && changedProperties.Contains("Ipv6BlockText") && changedProperties.Contains("LastActivity"),
    "Policy or activity changes do not update existing rows");
clients[1].WhitelistCurrent = null;
Check(clients[1].Whitelist == "Недоступен", "Unknown firewall revision displayed as applied");
clients[1].Policy = new frontend.Models.ClientPolicyStatus { BlockIpv6 = true };
Check(clients[1].Ipv6BlockText == "Вкл" && !clients[1].Policy!.CanSetIpv6Block,
    "IPv6 state did not update or legacy client gained control capability");
foreach (var enabled in new[] { true, false })
    await WhitelistReply(name, async service => { await service.SetClientIpv6BlockAsync("Клиент:1", enabled); return true; },
        ["SET_CLIENT_IPV6_BLOCK", "14", "Клиент:1", "1", enabled ? "1" : "0"], []);
try
{
    await WhitelistReply(name, async service => { await service.SetClientIpv6BlockAsync("old", true); return true; },
        ["SET_CLIENT_IPV6_BLOCK", "3", "old", "1", "1"], ["unsupported client"], "ERROR");
    throw new Exception("IPv6 control rejection was ignored");
}
catch (IOException ex) { Check(ex.Message == "unsupported client", "IPv6 control rejection details lost"); }

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
    await WhitelistReply(name, service => service.GetWhitelistsAsync(), ["GET_WHITELISTS_DETAILS"], ["load failed"], "ERROR");
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
UserTableViewTests.Run();
UserOverviewTests.Run();
Console.WriteLine("Connectivity, certificate fingerprint, client policy, user overview/selection and whitelist pipe tests passed.");
