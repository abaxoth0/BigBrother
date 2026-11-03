using System.IO.Pipes;
using System.Text;
using System.Threading.Channels;
using frontend.Services;

static class SubscriptionTests
{
    public static async Task RunAsync()
    {
        var name = "BigBrother.Subscription.Test." + Guid.NewGuid().ToString("N");
        using var ipc = new IpcService(name);
        var refreshes = Channel.CreateUnbounded<bool>();
        ipc.StateChanged += () => refreshes.Writer.TryWrite(true);
        using var timeout = new CancellationTokenSource(TimeSpan.FromSeconds(10));
        for (int attempt = 0; attempt < 2; attempt++)
        {
            using (var server = new NamedPipeServerStream(name, PipeDirection.InOut, 1,
                       PipeTransmissionMode.Byte, PipeOptions.Asynchronous))
            {
                if (attempt == 0) ipc.StartEventSubscription();
                await server.WaitForConnectionAsync(timeout.Token);
                using var reader = new StreamReader(server, leaveOpen: true);
                if (await reader.ReadLineAsync(timeout.Token) != "SUBSCRIBE" ||
                    await reader.ReadLineAsync(timeout.Token) != "")
                    throw new Exception("Wrong subscription request");
                await server.WriteAsync(Encoding.UTF8.GetBytes("OK\n\n"), timeout.Token);
                await server.FlushAsync(timeout.Token);
                await refreshes.Reader.ReadAsync(timeout.Token);
                if (refreshes.Reader.TryRead(out _)) throw new Exception("Unexpected duplicate refresh");
            }
            await refreshes.Reader.ReadAsync(timeout.Token);
        }
        ipc.StopEventSubscription();
        Console.WriteLine("Subscription tests passed: refresh on connection, backend shutdown and reconnect.");
    }
}
