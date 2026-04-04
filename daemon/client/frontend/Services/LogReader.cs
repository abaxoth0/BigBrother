using System.IO;
using Lib;

namespace frontend.Services;

public class LogReader
{
    private string _currentFile = "";
    private long _lastPosition;
    private bool _isRunning;
    private LogSource _source = LogSource.Unknown;
    private bool _initialLoadDone;
    
    private CancellationTokenSource? _cts;
    private Task? _readTask;
    private readonly object _lock = new();
    
    private const int InitialReadKB = 512; // Read last 512KB on startup

    public event Action<string>? OnNewLine;
    public string CurrentFile => _currentFile;
    public bool IsRunning => _isRunning;

    public void Start(string filePath, LogSource source = LogSource.Unknown, bool loadFullHistory = false)
    {
        Stop();

        _source = source;

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
            OnNewLine?.Invoke($"Log file not found: {filePath}");
            return;
        }

        _currentFile = filePath;
        _lastPosition = 0;
        _initialLoadDone = false;

        try
        {
            _isRunning = true;
            _cts = new CancellationTokenSource();
            _readTask = Task.Run(async () => 
            {
                try
                {
                    await BackgroundReadLoop(_cts.Token);
                }
                catch { }
            });
            
            OnNewLine?.Invoke($"Started reading log: {filePath}");
        }
        catch (Exception ex)
        {
            OnNewLine?.Invoke($"Failed to open log file: {ex.Message}");
        }
    }

    private async Task BackgroundReadLoop(CancellationToken ct)
    {
        byte[] buffer = new byte[65536]; // 64KB buffer for fewer reads
        
        while (!ct.IsCancellationRequested && _isRunning)
        {
            try
            {
                await Task.Delay(100, ct);
                if (!_isRunning) break;
                
                ReadNewEntries(buffer);
            }
            catch (TaskCanceledException)
            {
                break;
            }
            catch { }
        }
    }

    private void ReadNewEntries(byte[] buffer)
    {
        if (!_isRunning) return;

        try
        {
            lock (_lock)
            {
                if (!File.Exists(_currentFile)) return;
                
                var fileInfo = new FileInfo(_currentFile);
                
                // On first read, skip to near end to avoid loading old logs
                if (!_initialLoadDone && fileInfo.Length > InitialReadKB * 1024)
                {
                    _lastPosition = fileInfo.Length - (InitialReadKB * 1024);
                    _initialLoadDone = true;
                }
                
                if (fileInfo.Length <= _lastPosition) return;
                
                using var fs = new FileStream(_currentFile, FileMode.Open, FileAccess.Read, FileShare.ReadWrite | FileShare.Delete);
                fs.Seek(_lastPosition, SeekOrigin.Begin);
                
                int bytesRead = fs.Read(buffer, 0, buffer.Length);
                if (bytesRead <= 0) return;
                
                var entries = LogParser.ParseAll(buffer.Take(bytesRead).ToArray());
                
                if (entries.Count == 0) 
                {
                    _lastPosition += bytesRead;
                    return;
                }
                
                var lines = new List<string>();
                foreach (var entry in entries)
                {
                    if (_source != LogSource.Frontend && entry.Level == LogLevel.Info)
                        continue;
                    
                    entry.Source = _source;
                    
                    string levelStr = entry.Level switch
                    {
                        LogLevel.Info => "INFO",
                        LogLevel.Error => "ERROR",
                        LogLevel.Debug => "DEBUG",
                        LogLevel.Blocked => "BLOCKED",
                        _ => "UNK"
                    };
                    
                    string sourceStr = entry.Source switch
                    {
                        LogSource.Firewall => "FW",
                        LogSource.Client => "CL",
                        LogSource.Frontend => "FE",
                        _ => "??"
                    };
                    
                    lines.Add($"[{entry.Timestamp:HH:mm:ss}] [{sourceStr}] [{levelStr}] {entry.Message}");
                }
                
                if (lines.Count > 0)
                {
                    _lastPosition += bytesRead;
                    
                    var finalLines = lines;
                    System.Windows.Application.Current?.Dispatcher.BeginInvoke(() =>
                    {
                        foreach (var line in finalLines)
                        {
                            OnNewLine?.Invoke(line);
                        }
                    });
                }
                else
                {
                    _lastPosition += bytesRead;
                }
            }
        }
        catch { }
    }

    public void Stop()
    {
        _isRunning = false;
        
        _cts?.Cancel();
        _cts?.Dispose();
        _cts = null;
        
        _readTask = null;
    }
}