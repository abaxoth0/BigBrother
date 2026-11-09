using System.IO.Pipes;
using System.Text;
using frontend.Services;

internal static class BrokerTests
{
    public static async Task RunAsync()
    {
        string name = "BigBrother.Broker.Test." + Guid.NewGuid().ToString("N");
        using var ipc = new IpcService(name);
        async Task<T> Exchange<T>(Func<Task<T>> invoke, string command, string[] arguments, string[] reply, bool ok = true)
        {
            using var server = new NamedPipeServerStream(name, PipeDirection.InOut, 1, PipeTransmissionMode.Byte, PipeOptions.Asynchronous);
            using var timeout = new CancellationTokenSource(TimeSpan.FromSeconds(5));
            var pending = invoke();
            await server.WaitForConnectionAsync(timeout.Token);
            using var input = new StreamReader(server, leaveOpen: true);
            if (await input.ReadLineAsync(timeout.Token) != command) throw new Exception("Wrong broker command.");
            foreach (var argument in arguments)
            {
                if (await input.ReadLineAsync(timeout.Token) != Encoding.UTF8.GetByteCount(argument).ToString())
                    throw new Exception("Wrong broker argument length.");
                var chars = new char[argument.Length];
                if (await input.ReadBlockAsync(chars, timeout.Token) != chars.Length) throw new Exception("Truncated argument.");
                if (new string(chars) != argument || await input.ReadLineAsync(timeout.Token) != "")
                    throw new Exception("Wrong broker argument.");
            }
            if (await input.ReadLineAsync(timeout.Token) != "") throw new Exception("Missing terminator.");
            string payload = (ok ? "OK\n" : "ERROR\n") + string.Concat(reply.Select(value =>
                Encoding.UTF8.GetByteCount(value) + "\n" + value + "\n")) + "\n";
            await server.WriteAsync(Encoding.UTF8.GetBytes(payload), timeout.Token);
            await server.FlushAsync(timeout.Token);
            return await pending;
        }

        foreach (var action in new[] { "start", "stop", "restart" })
            if (!await Exchange(() => ipc.ControlFirewallAsync(action), action.ToUpperInvariant() + "_FIREWALL", [], []))
                throw new Exception("Firewall control did not use the backend.");
        if (await Exchange(() => ipc.ControlFirewallAsync("start"), "START_FIREWALL", [], ["failed"], false))
            throw new Exception("Failed service start was reported as successful.");
        var chunk = await Exchange(() => ipc.ReadLogChunkAsync("firewall", 12, "old"),
            "READ_LOG", ["firewall", "12", "old"], ["new", "3", "3", "00abff"]);
        if (chunk is not { Identity: "new", Offset: 3, Length: 3 } || !chunk.Value.Bytes.SequenceEqual(new byte[] { 0, 171, 255 }))
            throw new Exception("Broker log bytes were corrupted.");
        if (await Exchange(() => ipc.ReadLogChunkAsync("client", 0),
                "READ_LOG", ["client", "0", ""], ["id", "0", "0", "invalid"]) != null)
            throw new Exception("Invalid log data was accepted.");
        if (!await Exchange(() => ipc.InstallClientUpdateAsync("{\"version\":\"3.0.0\"}"),
                "INSTALL_CLIENT_UPDATE", ["{\"version\":\"3.0.0\"}"], []))
            throw new Exception("Update preparation did not use the backend.");
        if (await Exchange(ipc.GetApprovedUpdateAsync, "GET_APPROVED_UPDATE", [], ["{}"]) != "{}")
            throw new Exception("Update worker did not retrieve its approved metadata.");
        Console.WriteLine("Broker tests passed: firewall controls, service failures, binary logs, invalid data and update worker coordination.");
    }
}
