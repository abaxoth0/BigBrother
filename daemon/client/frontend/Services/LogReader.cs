using System.IO;

namespace frontend.Services;

public class LogReader
{
    private FileStream? _fileStream;
    private StreamReader? _reader;
    private FileSystemWatcher? _watcher;
    private string _currentFile = "";
    private long _lastPosition;
    private bool _isRunning;

    public event Action<string>? OnNewLine;
    public string CurrentFile => _currentFile;
    public bool IsRunning => _isRunning;

    public void Start(string filePath)
    {
        Stop();

        if (!File.Exists(filePath))
        {
            OnNewLine?.Invoke($"[INFO] Log file not found: {filePath}");
            return;
        }

        _currentFile = filePath;
        _lastPosition = 0;

        try
        {
            _fileStream = new FileStream(filePath, FileMode.Open, FileAccess.Read, FileShare.ReadWrite | FileShare.Delete);
            _reader = new StreamReader(_fileStream);

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
            OnNewLine?.Invoke($"[INFO] Started reading log: {filePath}");
        }
        catch (Exception ex)
        {
            OnNewLine?.Invoke($"[ERROR] Failed to open log file: {ex.Message}");
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
        if (_reader == null || _fileStream == null || !_isRunning) return;

        try
        {
            _fileStream.Seek(_lastPosition, SeekOrigin.Begin);
            _reader.DiscardBufferedData();

            string? line;
            while ((line = _reader.ReadLine()) != null)
            {
                if (!string.IsNullOrWhiteSpace(line))
                {
                    OnNewLine?.Invoke(line);
                }
            }

            _lastPosition = _fileStream.Position;
        }
        catch { }
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

        _reader?.Dispose();
        _reader = null;

        _fileStream?.Dispose();
        _fileStream = null;
    }
}
