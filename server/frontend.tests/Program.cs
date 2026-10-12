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
Console.WriteLine("Connectivity and certificate fingerprint pipe tests passed.");
