using System.IO.Pipes;
using System.Text;
using frontend.Services;

var name = "BigBrother.SavedPin.Test." + Guid.NewGuid().ToString("N");
using var ipc = new IpcService(name);
var pin = new string('A', 64);
foreach (var (reply, expected) in new (string, string?)[]
{
    ($"OK\n64\n{pin}\n\n", pin),
    ("OK\n0\n\n\n", ""),
    ("ERROR\n15\nunknown command\n\n", null),
    ($"OK\n64\n{pin}\n\n", pin),
})
{
    using var server = new NamedPipeServerStream(name, PipeDirection.InOut, 1,
        PipeTransmissionMode.Byte, PipeOptions.Asynchronous);
    using var timeout = new CancellationTokenSource(TimeSpan.FromSeconds(5));
    var read = ipc.GetSavedServerFingerprintAsync();
    await server.WaitForConnectionAsync(timeout.Token);
    using var reader = new StreamReader(server, leaveOpen: true);
    if (await reader.ReadLineAsync(timeout.Token) != "GET_SAVED_SERVER_FINGERPRINT" ||
        await reader.ReadLineAsync(timeout.Token) != "") throw new Exception("Wrong request");
    await server.WriteAsync(Encoding.UTF8.GetBytes(reply), timeout.Token);
    await server.FlushAsync(timeout.Token);
    if (await read != expected) throw new Exception("Wrong saved fingerprint result");
    if (expected == null && !ipc.SavedFingerprintError.Contains("unknown command"))
        throw new Exception("Missing daemon compatibility error");
}
Console.WriteLine("Saved fingerprint tests passed: persisted value, empty pin, old daemon, retry.");

foreach (var (reply, expected) in new (string, string[]?)[]
{
    ("OK\n11\nexample.com\n12\n!blocked.com\n\n", new[] { "example.com", "!blocked.com" }),
    ("OK\n\n", Array.Empty<string>()),
    ("ERROR\n23\nfailed to get whitelist\n\n", null),
    ("OK\n\n", Array.Empty<string>()),
    ("OK\n11\nexample.org\n\n", new[] { "example.org" }),
})
{
    using var server = new NamedPipeServerStream(name, PipeDirection.InOut, 1,
        PipeTransmissionMode.Byte, PipeOptions.Asynchronous);
    using var timeout = new CancellationTokenSource(TimeSpan.FromSeconds(5));
    var read = ipc.GetWhitelistAsync();
    await server.WaitForConnectionAsync(timeout.Token);
    using var reader = new StreamReader(server, leaveOpen: true);
    if (await reader.ReadLineAsync(timeout.Token) != "GET_WHITELIST" ||
        await reader.ReadLineAsync(timeout.Token) != "") throw new Exception("Wrong whitelist request");
    await server.WriteAsync(Encoding.UTF8.GetBytes(reply), timeout.Token);
    await server.FlushAsync(timeout.Token);
    var result = await read;
    if (expected == null ? result != null : result == null || !result.SequenceEqual(expected))
        throw new Exception("Wrong whitelist result");
}
if (await ipc.GetWhitelistAsync() != null) throw new Exception("Unavailable backend returned a whitelist");
Console.WriteLine("Whitelist pipe tests passed: populated, empty, failed request, retry, re-enabled and unavailable backend.");

var notes = "{\"version\":\"1.4.0\",\"release_notes\":\"" + new string('я', 12000) + "\"}";
foreach (var (reply, expected, fail) in new[] {
    ($"OK\n{Encoding.UTF8.GetByteCount(notes)}\n{notes}\n\n", notes, false),
    ("OK\n\n", "", false),
    ("ERROR\n11\nserver down\n\n", "", true)
})
{
    using var server = new NamedPipeServerStream(name, PipeDirection.InOut, 1, PipeTransmissionMode.Byte, PipeOptions.Asynchronous);
    using var timeout = new CancellationTokenSource(TimeSpan.FromSeconds(5));
    var read = ipc.GetClientUpdateAsync();
    await server.WaitForConnectionAsync(timeout.Token);
    using var reader = new StreamReader(server, leaveOpen: true);
    if (await reader.ReadLineAsync(timeout.Token) != "GET_CLIENT_UPDATE" || await reader.ReadLineAsync(timeout.Token) != "")
        throw new Exception("Wrong update metadata request.");
    await server.WriteAsync(Encoding.UTF8.GetBytes(reply), timeout.Token);
    await server.FlushAsync(timeout.Token);
    try
    {
        var result = await read;
        if (fail || result != expected) throw new Exception("Wrong update metadata result.");
    }
    catch (IOException) when (fail) { }
}
Console.WriteLine("Update pipe tests passed: long Unicode notes, no publication and server errors.");
