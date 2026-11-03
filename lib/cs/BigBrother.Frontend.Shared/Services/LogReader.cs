using System.IO;
using Lib;

namespace frontend.Services;

public class LogReader
{
    private LogFileMonitor? _monitor;
    private int _generation;
    private const int InitialReadBytes = 512 * 1024;

    public event Action<string>? OnNewLine;
    public string CurrentFile { get; private set; } = "";
    public bool IsRunning => _monitor != null;

    public void Start(string filePath, LogSource source = LogSource.Unknown, bool loadFullHistory = false)
    {
        Stop();
        if (source == LogSource.Unknown)
        {
            string name = Path.GetFileName(filePath).ToLowerInvariant();
            source = name.Contains("firewall") ? LogSource.Firewall :
                name.Contains("client") ? LogSource.Client : LogSource.Frontend;
        }
        if (!File.Exists(filePath))
        {
            OnNewLine?.Invoke($"Log file not found: {filePath}");
            return;
        }

        CurrentFile = filePath;
        int generation = _generation;
        long position = 0;
        bool initial = true;
        var buffer = new byte[128 * 1024];
        int retained = 0;
        _monitor = new LogFileMonitor(Path.GetDirectoryName(Path.GetFullPath(filePath))!,
            Path.GetFileName(filePath), () =>
        {
            if (generation != Volatile.Read(ref _generation)) return;
            using var stream = new FileStream(filePath, FileMode.Open, FileAccess.Read,
                FileShare.ReadWrite | FileShare.Delete);
            long end = stream.Length;
            if (end < position)
            {
                position = 0;
                retained = 0;
                initial = true;
            }
            if (initial)
            {
                position = loadFullHistory ? 0 : Math.Max(0, end - InitialReadBytes);
                initial = false;
            }
            stream.Position = position;
            while (position < end && generation == Volatile.Read(ref _generation))
            {
                int read = stream.Read(buffer, retained, (int)Math.Min(buffer.Length - retained, end - position));
                if (read == 0) break;
                position += read;
                int length = retained + read;
                var data = buffer.AsSpan(0, length).ToArray();
                var entries = LogParser.ParseAvailable(data, out int consumed);
                retained = length - consumed;
                Buffer.BlockCopy(buffer, consumed, buffer, 0, retained);
                foreach (var entry in entries)
                {
                    if (generation != Volatile.Read(ref _generation)) return;
                    string level = entry.Level switch
                    {
                        LogLevel.Info => "INFO", LogLevel.Error => "ERROR",
                        LogLevel.Debug => "DEBUG", LogLevel.Blocked => "BLOCKED", _ => "UNK"
                    };
                    string label = source switch
                    {
                        LogSource.Firewall => "FW", LogSource.Client => "CL",
                        LogSource.Frontend => "FE", _ => "??"
                    };
                    OnNewLine?.Invoke($"[{entry.Timestamp:HH:mm:ss}] [{label}] [{level}] {entry.Message}");
                }
            }
        });
        OnNewLine?.Invoke($"Started reading log: {filePath}");
    }

    public void Stop()
    {
        Interlocked.Increment(ref _generation);
        _monitor?.Dispose();
        _monitor = null;
    }
}
