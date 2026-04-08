using System.Collections.ObjectModel;
using System.IO;
using System.Windows;
using System.Windows.Input;
using frontend.Models;

namespace frontend.ViewModels;

public class RelayCommand : ICommand
{
    private readonly Action<object?> _execute;
    private readonly Func<object?, bool>? _canExecute;

    public RelayCommand(Action<object?> execute, Func<object?, bool>? canExecute = null)
    {
        _execute = execute;
        _canExecute = canExecute;
    }

    public event EventHandler? CanExecuteChanged
    {
        add => CommandManager.RequerySuggested += value;
        remove => CommandManager.RequerySuggested -= value;
    }

    public bool CanExecute(object? parameter) => _canExecute?.Invoke(parameter) ?? true;
    public void Execute(object? parameter) => _execute(parameter);
}

public class MainViewModel : ViewModelBase
{
    private const int MaxLogs = 500;

    // Status
    private string _daemonStatus = "Запущен";
    private string _clientStatus = "Запущен";
    private string _daemonConnectionStatus = "...";
    private string _clientConnectionStatus = "...";
    private string _hostName = "DESKTOP-PC";
    private string _ipAddress = "192.168.1.100";
    private int _daemonPid = 1234;
    private int _clientPid = 5678;
    private int _whitelistCount = 5;
    private DateTime _lastUpdate = DateTime.Now;
    private bool _autoScroll = true;

    public bool AutoScroll
    {
        get => _autoScroll;
        set => SetProperty(ref _autoScroll, value);
    }

    public string DaemonStatus
    {
        get => _daemonStatus;
        set => SetProperty(ref _daemonStatus, value);
    }

    public string ClientStatus
    {
        get => _clientStatus;
        set => SetProperty(ref _clientStatus, value);
    }

    public string DaemonConnectionStatus
    {
        get => _daemonConnectionStatus;
        set => SetProperty(ref _daemonConnectionStatus, value);
    }

    public string ClientConnectionStatus
    {
        get => _clientConnectionStatus;
        set => SetProperty(ref _clientConnectionStatus, value);
    }

    public string HostName
    {
        get => _hostName;
        set => SetProperty(ref _hostName, value);
    }

    public string IpAddress
    {
        get => _ipAddress;
        set => SetProperty(ref _ipAddress, value);
    }

    public int DaemonPid
    {
        get => _daemonPid;
        set => SetProperty(ref _daemonPid, value);
    }

    public int ClientPid
    {
        get => _clientPid;
        set => SetProperty(ref _clientPid, value);
    }

    public int WhitelistCount
    {
        get => _whitelistCount;
        set => SetProperty(ref _whitelistCount, value);
    }

    public DateTime LastUpdate
    {
        get => _lastUpdate;
        set
        {
            if (SetProperty(ref _lastUpdate, value))
                OnPropertyChanged(nameof(LastUpdateFormatted));
        }
    }

    public string LastUpdateFormatted => $"Последнее обновление: {LastUpdate:HH:mm:ss}";

    // Logs
    private string _logSearchText = "";
    public string LogSearchText
    {
        get => _logSearchText;
        set
        {
            if (SetProperty(ref _logSearchText, value))
                UpdateFilteredLogs();
        }
    }

    public ObservableCollection<LogEntry> Logs { get; } = new();
    public ObservableCollection<LogEntry> FilteredLogs { get; } = new();

    // Whitelist
    private bool _showPrettyWhitelist = true;
    public bool ShowPrettyWhitelist
    {
        get => _showPrettyWhitelist;
        set
        {
            if (SetProperty(ref _showPrettyWhitelist, value))
                UpdateWhitelistDisplay();
        }
    }

    private int _whitelistFormatIndex = 0;
    public int WhitelistFormatIndex
    {
        get => _whitelistFormatIndex;
        set
        {
            if (SetProperty(ref _whitelistFormatIndex, value))
                ShowPrettyWhitelist = value == 0;
        }
    }

    private string _rawWhitelistText = "";
    public string RawWhitelistText
    {
        get => _rawWhitelistText;
        private set => SetProperty(ref _rawWhitelistText, value);
    }

    private string _whitelistSearchText = "";
    public string WhitelistSearchText
    {
        get => _whitelistSearchText;
        set
        {
            if (SetProperty(ref _whitelistSearchText, value))
                UpdateWhitelistDisplay();
        }
    }

    public ObservableCollection<WhitelistEntry> WhitelistEntries { get; } = new();
    public ObservableCollection<string> FilteredWhitelist { get; } = new();

    // Commands
    public ICommand ClearLogsCommand { get; }
    public ICommand ExportLogsCommand { get; }

    // Event for auto-scroll notification
    public event Action? ScrollToBottomRequested;

    public MainViewModel()
    {
        ClearLogsCommand = new RelayCommand(_ => ClearLogs());
        ExportLogsCommand = new RelayCommand(_ => ExportLogs());

        AddLog("INFO", "Клиент запущен");
        AddLog("INFO", "Подключение к демону...");
        AddLog("INFO", "Демон подключен");
        AddLog("WARNING", "Тестовое предупреждение");
        AddLog("ERROR", "Тестовая ошибка");
        AddLog("DNS", "github.com -> 140.82.121.4");
        AddLog("BLOCKED", "TCP BLOCKED: 140.82.121.4:443");
        AddLog("INFO", "Whitelist reloaded (+3 domains)");

        // Sample whitelist entries
        var sampleWhitelist = new[]
        {
            "google.com",
            "*.github.com",
            "\"microsoft\"",
            "wikipedia.org",
            "figma.com"
        };

        foreach (var entry in sampleWhitelist)
        {
            WhitelistEntries.Add(WhitelistEntry.Parse(entry));
        }

        UpdateWhitelistDisplay();
        LastUpdate = DateTime.Now;
    }

    private readonly List<string> _pendingLogs = new();
    private const int LogBatchSize = 10; // Smaller batches for more responsive UI
    
    public void AddLog(string level, string message)
    {
        _pendingLogs.Add(message);
        
        if (_pendingLogs.Count >= LogBatchSize)
        {
            FlushPendingLogs();
        }
    }
    
    public void FlushPendingLogs()
    {
        if (_pendingLogs.Count == 0) return;
        
        var toAdd = new List<string>(_pendingLogs);
        _pendingLogs.Clear();
        
        System.Windows.Application.Current.Dispatcher.BeginInvoke(() =>
        {
            int logsToRemove = Logs.Count + toAdd.Count - MaxLogs;
            
            if (logsToRemove > 0)
            {
                int removeCount = Math.Min(logsToRemove, Logs.Count);
                for (int i = 0; i < removeCount; i++)
                {
                    Logs.RemoveAt(0);
                }
            }
            
            foreach (var msg in toAdd)
            {
                string level = "INFO";
                if (msg.Contains("[ERROR]")) level = "ERROR";
                else if (msg.Contains("[WARNING]") || msg.Contains("[WARN]")) level = "WARNING";
                else if (msg.Contains("[DNS]")) level = "DNS";
                else if (msg.Contains("[BLOCKED]")) level = "BLOCKED";
                
                var entry = new LogEntry
                {
                    Timestamp = DateTime.Now.ToString("HH:mm:ss"),
                    Level = level,
                    Message = msg
                };
                
                Logs.Add(entry);
            }
            
            if (!string.IsNullOrEmpty(LogSearchText))
            {
                UpdateFilteredLogs();
            }
            else
            {
                FilteredLogs.Clear();
                foreach (var log in Logs)
                {
                    FilteredLogs.Add(log);
                }
            }

            if (AutoScroll)
            {
                ScrollToBottomRequested?.Invoke();
            }
        });
    }
    
    // Clear pending logs if they get too large (prevents memory buildup)
    public void TrimPendingLogs()
    {
        if (_pendingLogs.Count > MaxLogs * 2)
        {
            var keepCount = _pendingLogs.Count - MaxLogs;
            _pendingLogs.RemoveRange(0, keepCount);
        }
    }

    private void UpdateFilteredLogs()
    {
        FilteredLogs.Clear();
        
        // Only filter if there's search text - otherwise show all
        if (string.IsNullOrEmpty(LogSearchText))
        {
            foreach (var log in Logs)
            {
                FilteredLogs.Add(log);
            }
            return;
        }
        
        var searchLower = LogSearchText.ToLower();
        foreach (var log in Logs)
        {
            if (log.Message.ToLower().Contains(searchLower) ||
                log.Level.ToLower().Contains(searchLower))
            {
                FilteredLogs.Add(log);
            }
        }
    }

    private void UpdateWhitelistDisplay()
    {
        FilteredWhitelist.Clear();
        var searchLower = WhitelistSearchText?.ToLower() ?? "";

        foreach (var entry in WhitelistEntries)
        {
            var displayText = ShowPrettyWhitelist ? entry.GetPrettyDescription() : entry.Raw;

            if (string.IsNullOrEmpty(searchLower) ||
                displayText.ToLower().Contains(searchLower) ||
                entry.Raw.ToLower().Contains(searchLower))
            {
                FilteredWhitelist.Add(displayText);
            }
        }
        WhitelistCount = FilteredWhitelist.Count;

        UpdateRawWhitelistText();
    }

    private void UpdateRawWhitelistText()
    {
        var lines = WhitelistEntries
            .Select(e => e.Raw);
        RawWhitelistText = string.Join(Environment.NewLine, lines);
    }

    public void ClearLogs()
    {
        Logs.Clear();
        FilteredLogs.Clear();
        AddLog("INFO", "Логи очищены");
    }

    public void ExportLogs()
    {
        var dialog = new Microsoft.Win32.SaveFileDialog
        {
            Filter = "Text files (*.txt)|*.txt|All files (*.*)|*.*",
            DefaultExt = ".txt",
            FileName = $"logs_{DateTime.Now:yyyyMMdd_HHmmss}.txt"
        };

        if (dialog.ShowDialog() == true)
        {
            var lines = Logs.Select(l => $"[{l.Timestamp}] {l.Message}");
            File.WriteAllLines(dialog.FileName, lines);
            AddLog("INFO", $"Логи сохранены: {dialog.FileName}");
        }
    }

    public void ShowNotification(string title, string message)
    {
        // Windows Toast Notification placeholder
        // Would use Windows.UI.Notifications or similar
        AddLog("INFO", $"Уведомление: {title} - {message}");
    }

    // Commands (placeholders)
    public void StartDaemon() { }
    public void StopDaemon() { }
    public void RestartDaemon() { }
    public void StartClient() { }
    public void StopClient() { }
    public void RestartClient() { }
}
