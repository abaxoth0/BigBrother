using System.IO;
using System.Text.Json;
using frontend.Models;

namespace frontend.Services;

/// <summary>
/// Tails the server backend's JSON-lines log file. Encapsulates FileSystemWatcher
/// lifecycle (start/re-arm/bootstrap retry), event debouncing, file position
/// tracking, and JSON parsing. Parsed entries are raised via <see cref="EntriesRead"/>.
/// </summary>
public class ServerLogTailer : IDisposable
{
    private const int DebounceMs = 250;
    private const int BootstrapMs = 2000;
    private const int InitialReadKB = 512;
    private const int MaxEntries = 2000;

    private string _logPath = "";
    private FileSystemWatcher? _watcher;
    private System.Timers.Timer? _debounceTimer;
    private System.Timers.Timer? _bootstrapTimer;
    private string _lastLogFile = "";
    private long _lastLogPosition;
    private bool _disposed;

    /// <summary>Raised with newly parsed log entries when the tailed file grows.</summary>
    public event Action<List<ServerLogEntry>>? EntriesRead;

    public void SetLogPath(string path)
    {
        _logPath = path;
        TryStart();
    }

    public void Start() => TryStart();

    public void Stop()
    {
        _disposed = true;
        if (_watcher != null)
        {
            _watcher.EnableRaisingEvents = false;
            _watcher.Changed -= OnChanged;
            _watcher.Created -= OnChanged;
            _watcher.Error -= OnError;
            _watcher.Dispose();
            _watcher = null;
        }
        StopBootstrap();
        StopDebounce();
    }

    public void Dispose()
    {
        Stop();
        _disposed = true;
    }

    private void TryStart()
    {
        if (string.IsNullOrEmpty(_logPath))
        {
            ScheduleBootstrap();
            return;
        }
        var dir = new DirectoryInfo(_logPath);
        if (!dir.Exists)
        {
            ScheduleBootstrap();
            return;
        }
        StopBootstrap();
        if (_watcher != null)
        {
            _watcher.EnableRaisingEvents = false;
            _watcher.Changed -= OnChanged;
            _watcher.Created -= OnChanged;
            _watcher.Error -= OnError;
            _watcher.Dispose();
            _watcher = null;
        }
        _watcher = new FileSystemWatcher(_logPath, "*.log")
        {
            NotifyFilter = NotifyFilters.LastWrite | NotifyFilters.FileName | NotifyFilters.CreationTime | NotifyFilters.Size,
            IncludeSubdirectories = false,
            InternalBufferSize = 64 * 1024,
            EnableRaisingEvents = true
        };
        _watcher.Changed += OnChanged;
        _watcher.Created += OnChanged;
        _watcher.Error += OnError;
        _lastLogFile = "";
        _lastLogPosition = 0;
        ReadNewEntries();
    }

    private void Rearm()
    {
        // Recreate the watcher after a buffer-overflow error WITHOUT resetting
        // _lastLogPosition/_lastLogFile (avoids re-reading the whole file).
        if (_watcher != null)
        {
            _watcher.EnableRaisingEvents = false;
            _watcher.Changed -= OnChanged;
            _watcher.Created -= OnChanged;
            _watcher.Error -= OnError;
            _watcher.Dispose();
            _watcher = null;
        }
        if (_disposed || string.IsNullOrEmpty(_logPath)) return;
        var dir = new DirectoryInfo(_logPath);
        if (!dir.Exists) return;
        _watcher = new FileSystemWatcher(_logPath, "*.log")
        {
            NotifyFilter = NotifyFilters.LastWrite | NotifyFilters.FileName | NotifyFilters.CreationTime | NotifyFilters.Size,
            IncludeSubdirectories = false,
            InternalBufferSize = 64 * 1024,
            EnableRaisingEvents = true
        };
        _watcher.Changed += OnChanged;
        _watcher.Created += OnChanged;
        _watcher.Error += OnError;
    }

    private void ScheduleBootstrap()
    {
        StopBootstrap();
        _bootstrapTimer = new System.Timers.Timer(BootstrapMs);
        _bootstrapTimer.Elapsed += (s, e) =>
        {
            if (_disposed) return;
            TryStart();
        };
        _bootstrapTimer.AutoReset = false;
        _bootstrapTimer.Start();
    }

    private void StopBootstrap()
    {
        if (_bootstrapTimer != null)
        {
            _bootstrapTimer.Stop();
            _bootstrapTimer.Dispose();
            _bootstrapTimer = null;
        }
    }

    private void StopDebounce()
    {
        if (_debounceTimer != null)
        {
            _debounceTimer.Stop();
            _debounceTimer.Dispose();
            _debounceTimer = null;
        }
    }

    private void OnChanged(object sender, FileSystemEventArgs e)
    {
        if (_disposed) return;
        // Coalesce the multiple FSW events fired per write (LastWrite + Size).
        if (_debounceTimer == null)
        {
            _debounceTimer = new System.Timers.Timer(DebounceMs);
            _debounceTimer.Elapsed += (s, args) =>
            {
                _debounceTimer.Stop();
                if (_disposed) return;
                ReadNewEntries();
            };
            _debounceTimer.AutoReset = false;
        }
        _debounceTimer.Stop();
        _debounceTimer.Start();
    }

    private void OnError(object sender, ErrorEventArgs e)
    {
        if (_disposed) return;
        Rearm();
    }

    private void ReadNewEntries()
    {
        if (_disposed || string.IsNullOrEmpty(_logPath)) return;

        try
        {
            var dir = new DirectoryInfo(_logPath);
            if (!dir.Exists) return;

            var files = dir.GetFiles("*.log");
            if (files.Length == 0) return;

            var file = files.OrderByDescending(f => f.LastWriteTime).First();

            using var stream = new FileStream(file.FullName, FileMode.Open, FileAccess.Read, FileShare.ReadWrite);

            // New log file (e.g., after restart) — read from near the end
            if (_lastLogFile != file.FullName)
            {
                _lastLogFile = file.FullName;
                _lastLogPosition = Math.Max(0, stream.Length - InitialReadKB * 1024);
            }

            if (_lastLogPosition >= stream.Length) return;

            stream.Seek(_lastLogPosition, SeekOrigin.Begin);
            using var reader = new StreamReader(stream);
            var newEntries = new List<ServerLogEntry>();
            long lastPos = _lastLogPosition;

            while (true)
            {
                var line = reader.ReadLine();
                if (line == null) break;
                lastPos = stream.Position;

                try
                {
                    using var doc = JsonDocument.Parse(line);
                    var root = doc.RootElement;

                    var ts = root.TryGetProperty("ts", out var tse) ? tse.GetString() ?? "" : "";
                    var level = root.TryGetProperty("level", out var le) ? le.GetString() ?? "" : "";
                    var src = root.TryGetProperty("source", out var se) ? se.GetString() ?? "" : "";
                    var msg = root.TryGetProperty("msg", out var me) ? me.GetString() ?? "" : "";

                    if (!string.IsNullOrEmpty(msg))
                    {
                        var time = ts.Length >= 19 ? ts.Substring(11, 8) : ts;
                        newEntries.Add(new ServerLogEntry
                        {
                            Timestamp = time,
                            Level = level,
                            Source = src,
                            Message = msg
                        });
                    }
                }
                catch { }
            }

            _lastLogPosition = lastPos;

            if (newEntries.Count > 0)
            {
                EntriesRead?.Invoke(newEntries);
            }
        }
        catch { }
    }
}
