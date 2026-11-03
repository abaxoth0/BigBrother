using System.IO;
using System.Windows.Input;
using frontend.Models;
using Lib;

namespace frontend.ViewModels;

public class LogSourceViewModel
{
    public string Label { get; set; } = "";
    public string FilePath { get; set; } = "";
    public List<Lib.LogEntry> AllEntries { get; set; } = new();
    public List<Lib.LogEntry> FilteredEntries { get; set; } = new();
}

public class LogViewViewModel : ViewModelBase, IDisposable
{
    private string _searchText = "";
    private string _statusText = "";
    private bool _isLoading;
    private bool _disposed;
    private readonly CancellationTokenSource _stop = new();

    public List<LogSourceViewModel> Sources { get; } = new();

    public string SearchText
    {
        get => _searchText;
        set { if (SetProperty(ref _searchText, value)) Filter(); }
    }

    public string StatusText
    {
        get => _statusText;
        set => SetProperty(ref _statusText, value);
    }

    public bool IsLoading
    {
        get => _isLoading;
        private set
        {
            if (SetProperty(ref _isLoading, value))
                CommandManager.InvalidateRequerySuggested();
        }
    }

    public ICommand ReloadCommand { get; }
    public ICommand ExportCommand { get; }
    public ICommand CloseCommand { get; }

    public event Action? ReloadRequested;
    public event Action? ExportRequested;
    public event Action? CloseRequested;

    public LogViewViewModel(List<LogFileSource> sources)
    {
        ReloadCommand = new RelayCommand(async _ => await ReloadAsync(), _ => !IsLoading && !_disposed);
        ExportCommand = new RelayCommand(_ => ExportRequested?.Invoke(), _ => !IsLoading && !_disposed);
        CloseCommand = new RelayCommand(_ => CloseRequested?.Invoke());

        foreach (var src in sources)
        {
            Sources.Add(new LogSourceViewModel
            {
                Label = src.Label,
                FilePath = src.FilePath
            });
        }
    }

    public async Task ReloadAsync()
    {
        if (IsLoading || _disposed) return;
        var token = _stop.Token;
        IsLoading = true;
        StatusText = "Загрузка логов…";
        var paths = Sources.Select(src => src.FilePath).ToArray();
        try
        {
            var loaded = await Task.Run(() => paths.Select(path => LoadLogFile(path, token)).ToArray(), token);
            if (_disposed) return;
            for (int i = 0; i < Sources.Count; i++) Sources[i].AllEntries = loaded[i];
            StatusText = $"Загружено: {loaded.Sum(entries => entries.Count)} записей из {Sources.Count} файлов";
            Filter();
        }
        catch (OperationCanceledException) when (token.IsCancellationRequested) { }
        finally
        {
            if (!_disposed) IsLoading = false;
        }
    }

    private static List<Lib.LogEntry> LoadLogFile(string path, CancellationToken token)
    {
        token.ThrowIfCancellationRequested();
        if (string.IsNullOrEmpty(path) || !File.Exists(path)) return new();

        try
        {
            using var fs = new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.ReadWrite | FileShare.Delete);
            var data = new byte[fs.Length];
            fs.ReadExactly(data, 0, data.Length);

            return LogParser.ParseAll(data, token);
        }
        catch (IOException) { return new(); }
        catch (UnauthorizedAccessException) { return new(); }
    }

    public void Filter()
    {
        if (_disposed) return;
        foreach (var src in Sources)
        {
            src.FilteredEntries = string.IsNullOrEmpty(SearchText)
                ? src.AllEntries
                : src.AllEntries
                    .Where(l => l.Message.Contains(SearchText, StringComparison.OrdinalIgnoreCase))
                    .ToList();
        }
        ReloadRequested?.Invoke();
    }

    public void Dispose()
    {
        if (_disposed) return;
        _disposed = true;
        _stop.Cancel();
        _stop.Dispose();
    }
}
