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
