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

public class LogViewViewModel : ViewModelBase
{
    private string _searchText = "";
    private string _statusText = "";

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

    public ICommand ReloadCommand { get; }
    public ICommand ExportCommand { get; }
    public ICommand CloseCommand { get; }

    public event Action? ReloadRequested;
    public event Action? ExportRequested;
    public event Action? CloseRequested;

    public LogViewViewModel(List<LogFileSource> sources)
    {
        ReloadCommand = new RelayCommand(_ => Reload());
        ExportCommand = new RelayCommand(_ => ExportRequested?.Invoke());
        CloseCommand = new RelayCommand(_ => CloseRequested?.Invoke());

        foreach (var src in sources)
        {
            Sources.Add(new LogSourceViewModel
            {
                Label = src.Label,
                FilePath = src.FilePath
            });
        }
        Reload();
    }

    public void Reload()
    {
        int totalEntries = 0;
        foreach (var src in Sources)
        {
            src.AllEntries = LoadLogFile(src.FilePath);
            totalEntries += src.AllEntries.Count;
        }
        StatusText = $"Загружено: {totalEntries} записей из {Sources.Count} файлов";
        ReloadRequested?.Invoke();
        Filter();
    }

    private List<Lib.LogEntry> LoadLogFile(string path)
    {
        var entries = new List<Lib.LogEntry>();
        if (string.IsNullOrEmpty(path) || !File.Exists(path)) return entries;

        try
        {
            using var fs = new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.ReadWrite | FileShare.Delete);
            var data = new byte[fs.Length];
            fs.ReadExactly(data, 0, data.Length);

            var parsed = LogParser.ParseAll(data);
            foreach (var entry in parsed)
            {
                entries.Add(entry);
            }
        }
        catch
        {
            // Unreadable/corrupt file — leave empty
        }

        return entries;
    }

    public void Filter()
    {
        var searchLower = SearchText?.ToLower() ?? "";
        foreach (var src in Sources)
        {
            src.FilteredEntries = string.IsNullOrEmpty(searchLower)
                ? src.AllEntries
                : src.AllEntries
                    .Where(l => l.Message.ToLower().Contains(searchLower))
                    .ToList();
        }
        ReloadRequested?.Invoke();
    }
}
