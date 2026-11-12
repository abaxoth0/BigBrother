using Lib;
using System.IO;

namespace frontend.Services;

public sealed class BrokerLogReader(string source, LogSource label)
{
    private CancellationTokenSource? _stop;
    public event Action<string>? OnNewLine;

    public void Start()
    {
        Stop();
        _stop = new CancellationTokenSource();
        _ = RunAsync(_stop.Token);
    }

    private async Task RunAsync(CancellationToken token)
    {
        using var ipc = new IpcService();
        long offset = -1;
        string identity = "";
        byte[] retained = [];
        try
        {
            while (!token.IsCancellationRequested)
            {
                var chunk = await ipc.ReadLogChunkAsync(source, offset, identity).ConfigureAwait(false);
                if (token.IsCancellationRequested) return;
                if (chunk is { } data)
                {
                    if (identity != data.Identity || data.Offset - data.Bytes.Length != offset) retained = [];
                    identity = data.Identity;
                    offset = data.Offset;
                    byte[] buffer = new byte[retained.Length + data.Bytes.Length];
                    retained.CopyTo(buffer, 0);
                    data.Bytes.CopyTo(buffer, retained.Length);
                    var entries = LogParser.ParseAvailable(buffer, out var consumed);
                    retained = buffer[consumed..];
                    foreach (var entry in entries)
                    {
                        if (token.IsCancellationRequested) return;
                        string level = entry.Level switch {
                            LogLevel.Info => "INFO", LogLevel.Error => "ERROR", LogLevel.Debug => "DEBUG",
                            LogLevel.Blocked => "BLOCKED", _ => "UNK"
                        };
                        OnNewLine?.Invoke($"[{entry.Timestamp:HH:mm:ss}] [{(label == LogSource.Firewall ? "FW" : "CL")}] [{level}] {entry.Message}");
                    }
                    if (data.Bytes.Length > 0 && offset < data.Length) continue;
                }
                await Task.Delay(500, token).ConfigureAwait(false);
            }
        }
        catch (OperationCanceledException) when (token.IsCancellationRequested) { }
        catch (IOException) { }
    }

    public void Stop()
    {
        _stop?.Cancel();
        _stop?.Dispose();
        _stop = null;
    }
}
