using System.IO;
using Lib;

namespace frontend.Services;

public class LogReader
{
    private FileStream? _fileStream;
    private FileSystemWatcher? _watcher;
    private string _currentFile = "";
    private long _lastPosition;
    private bool _isRunning;
    private readonly byte[] _readBuffer = new byte[8192];
    private LogSource _source = LogSource.Unknown;

    public event Action<string>? OnNewLine;
    public string CurrentFile => _currentFile;
    public bool IsRunning => _isRunning;

    public void Start(string filePath, LogSource source = LogSource.Unknown)
    {
        Stop();
        
        System.Diagnostics.Debug.WriteLine($"[LogReader] Start called with: {filePath}");

        _source = source;

        // Auto-detect source from file path if not specified
        if (source == LogSource.Unknown)
        {
            string fileName = Path.GetFileName(filePath).ToLower();
            if (fileName.Contains("firewall"))
                _source = LogSource.Firewall;
            else if (fileName.Contains("client"))
                _source = LogSource.Client;
            else
                _source = LogSource.Frontend;
        }

        if (!File.Exists(filePath))
        {
            System.Diagnostics.Debug.WriteLine($"[LogReader] File not found: {filePath}");
            OnNewLine?.Invoke($"Log file not found: {filePath}");
            return;
        }

        _currentFile = filePath;
        _lastPosition = 0;

        try
        {
            System.Diagnostics.Debug.WriteLine($"[LogReader] Opening file: {filePath}");
            _fileStream = new FileStream(filePath, FileMode.Open, FileAccess.Read, FileShare.ReadWrite | FileShare.Delete);

            var directory = Path.GetDirectoryName(filePath);
            var fileName = Path.GetFileName(filePath);

            if (directory != null)
            {
                _watcher = new FileSystemWatcher(directory, fileName)
                {
                    NotifyFilter = NotifyFilters.LastWrite | NotifyFilters.Size
                };
                _watcher.Changed += OnFileChanged;
                _watcher.EnableRaisingEvents = true;
            }

            _isRunning = true;
            System.Diagnostics.Debug.WriteLine($"[LogReader] Started reading: {filePath}");
            OnNewLine?.Invoke($"Started reading log: {filePath}");
            
            // Read existing content
            ReadNewLines();
        }
        catch (Exception ex)
        {
            System.Diagnostics.Debug.WriteLine($"[LogReader] Failed to open: {ex.Message}");
            OnNewLine?.Invoke($"Failed to open log file: {ex.Message}");
        }
    }

    private void OnFileChanged(object sender, FileSystemEventArgs e)
    {
        Task.Run(async () =>
        {
            await Task.Delay(100);
            ReadNewLines();
        });
    }

    private void ReadNewLines()
    {
        if (_fileStream == null || !_isRunning) return;

        try
        {
            _fileStream.Seek(_lastPosition, SeekOrigin.Begin);

            int bytesRead;
            while ((bytesRead = _fileStream.Read(_readBuffer, 0, _readBuffer.Length)) > 0)
            {
                System.Diagnostics.Debug.WriteLine($"[LogReader] Read {bytesRead} bytes from {_currentFile}");
                
                var entries = LogParser.ParseAll(_readBuffer.Take(bytesRead).ToArray());
                System.Diagnostics.Debug.WriteLine($"[LogReader] Parsed {entries.Count} entries");
                
                foreach (var entry in entries)
                {
                    // Filter: for non-frontend logs, show only DEBUG, ERROR, BLOCKED
                    if (_source != LogSource.Frontend && entry.Level == LogLevel.Info)
                        continue;
                    
                    entry.Source = _source;
                    string formatted = FormatEntry(entry);
                    System.Diagnostics.Debug.WriteLine($"[LogReader] Emit: {formatted}");
                    OnNewLine?.Invoke(formatted);
                }
            }

            _lastPosition = _fileStream.Position;
        }
        catch (Exception ex)
        {
            System.Diagnostics.Debug.WriteLine($"[LogReader] Error: {ex.Message}");
        }
    }

    private static string FormatEntry(LogEntry entry)
    {
        string levelStr = entry.Level switch
        {
            LogLevel.Info => "INFO",
            LogLevel.Error => "ERROR",
            LogLevel.Debug => "DEBUG",
            LogLevel.Blocked => "BLOCKED",
            _ => "UNKNOWN"
        };

        string sourceStr = entry.Source switch
        {
            LogSource.Firewall => "FW",
            LogSource.Client => "CL",
            LogSource.Frontend => "FE",
            _ => "??"
        };

        return $"[{entry.Timestamp:HH:mm:ss}] [{sourceStr}] [{levelStr}] {entry.Message}";
    }

    public void Stop()
    {
        _isRunning = false;

        if (_watcher != null)
        {
            _watcher.EnableRaisingEvents = false;
            _watcher.Changed -= OnFileChanged;
            _watcher.Dispose();
            _watcher = null;
        }

        _fileStream?.Dispose();
        _fileStream = null;
    }
}
