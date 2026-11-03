using System.IO;
using System.Text;
using System.Text.Json;
using frontend.Models;

namespace frontend.Services;

public class ServerLogTailer : IDisposable
{
    private readonly object _gate = new();
    private string _logPath = "";
    private LogFileMonitor? _monitor;
    private System.Threading.Timer? _bootstrap;
    private int _generation;
    private bool _disposed;

    public event Action<List<ServerLogEntry>>? EntriesRead;

    public void SetLogPath(string path)
    {
        lock (_gate)
        {
            StopMonitoring();
            _logPath = path;
            TryStart();
        }
    }

    public void Start()
    {
        lock (_gate) TryStart();
    }

    public void Stop()
    {
        lock (_gate) StopMonitoring();
    }

    public void Dispose()
    {
        lock (_gate)
        {
            _disposed = true;
            StopMonitoring();
        }
    }

    private void StopMonitoring()
    {
        Interlocked.Increment(ref _generation);
        _monitor?.Dispose();
        _monitor = null;
        _bootstrap?.Dispose();
        _bootstrap = null;
    }

    private void TryStart()
    {
        if (_disposed || _monitor != null) return;
        if (string.IsNullOrEmpty(_logPath) || !Directory.Exists(_logPath))
        {
            if (_bootstrap == null)
            {
                int bootstrapGeneration = _generation;
                _bootstrap = new System.Threading.Timer(_ =>
                {
                    lock (_gate)
                    {
                        if (bootstrapGeneration == _generation) TryStart();
                    }
                }, null, 2000, 2000);
            }
            return;
        }
        _bootstrap?.Dispose();
        _bootstrap = null;
        int generation = _generation;
        string path = _logPath;
        string lastFile = "";
        long position = 0;
        _monitor = new LogFileMonitor(path, "*.log", () =>
        {
            if (generation != Volatile.Read(ref _generation)) return;
            var file = new DirectoryInfo(path).GetFiles("*.log")
                .OrderByDescending(f => f.LastWriteTimeUtc).FirstOrDefault();
            if (file == null) return;
            using var stream = new FileStream(file.FullName, FileMode.Open, FileAccess.Read,
                FileShare.ReadWrite | FileShare.Delete);
            long end = stream.Length;
            if (lastFile != file.FullName)
            {
                lastFile = file.FullName;
                position = Math.Max(0, end - 512 * 1024);
            }
            if (end < position) position = 0;
            stream.Position = position;
            var buffer = new byte[64 * 1024];
            using var line = new MemoryStream();
            var entries = new Queue<ServerLogEntry>();
            long cursor = position;
            while (cursor < end && generation == Volatile.Read(ref _generation))
            {
                int read = stream.Read(buffer, 0, (int)Math.Min(buffer.Length, end - cursor));
                if (read == 0) break;
                for (int i = 0; i < read; i++)
                {
                    cursor++;
                    if (buffer[i] != (byte)'\n')
                    {
                        line.WriteByte(buffer[i]);
                        continue;
                    }
                    position = cursor;
                    try
                    {
                        using var doc = JsonDocument.Parse(Encoding.UTF8.GetString(line.GetBuffer(), 0, (int)line.Length));
                        var root = doc.RootElement;
                        string ts = root.TryGetProperty("ts", out var tse) ? tse.GetString() ?? "" : "";
                        string level = root.TryGetProperty("level", out var le) ? le.GetString() ?? "" : "";
                        string source = root.TryGetProperty("source", out var se) ? se.GetString() ?? "" : "";
                        string message = root.TryGetProperty("msg", out var me) ? me.GetString() ?? "" : "";
                        if (!string.IsNullOrEmpty(message))
                        {
                            entries.Enqueue(new ServerLogEntry
                            {
                                Timestamp = ts.Length >= 19 ? ts.Substring(11, 8) : ts,
                                Level = level, Source = source, Message = message
                            });
                            if (entries.Count > 2000) entries.Dequeue();
                        }
                    }
                    catch (JsonException) { }
                    catch (InvalidOperationException) { }
                    line.SetLength(0);
                }
            }
            if (entries.Count > 0 && generation == Volatile.Read(ref _generation))
                EntriesRead?.Invoke(entries.ToList());
        });
    }
}
