using System.Collections.Concurrent;
using System.Collections.ObjectModel;
using System.IO;
using System.Timers;
using System.Windows;
using System.Windows.Input;
using frontend.Models;
using frontend.Services;

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

public class MainViewModel : ViewModelBase, IDisposable
{
    private const int MaxLogs = 500;

    // Services
    private readonly IpcService _ipcService = new IpcService();

    // Status
    private string _daemonStatus = "Запущен";
    private string _clientStatus = "Запущен";
    private string _serverStatus = "Запущен";
    private string _daemonConnectionStatus = "...";
    private string _clientConnectionStatus = "...";
    private string _serverConnectionStatus = "...";
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

    public string ServerStatus
    {
        get => _serverStatus;
        set => SetProperty(ref _serverStatus, value);
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

    public string ServerConnectionStatus
    {
        get => _serverConnectionStatus;
        set => SetProperty(ref _serverConnectionStatus, value);
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

    // Settings
    private bool _fallbackWhitelistEnabled = true;
    public bool FallbackWhitelistEnabled
    {
        get => _fallbackWhitelistEnabled;
        set
        {
            if (SetProperty(ref _fallbackWhitelistEnabled, value))
            {
                _ = _ipcService.SetFallbackWhitelistEnabledAsync(value);
            }
        }
    }

    private string _serverAddress = "";
    public string ServerAddress
    {
        get => _serverAddress;
        set => SetProperty(ref _serverAddress, value);
    }

    private string _username = "";
    public string Username
    {
        get => _username;
        set => SetProperty(ref _username, value);
    }

    private string _serverName = "";
    public string ServerName
    {
        get => _serverName;
        set => SetProperty(ref _serverName, value);
    }

    private bool _settingsAvailable;
    public bool SettingsAvailable
    {
        get => _settingsAvailable;
        set => SetProperty(ref _settingsAvailable, value);
    }

    // Whitelist revision tracking for change detection
    private uint _lastWhitelistRevision = 0;
    private readonly System.Timers.Timer _statusTimer = new System.Timers.Timer(10000); // Poll every 10 seconds (reduced from 2s)

    public ObservableCollection<WhitelistEntry> WhitelistEntries { get; } = new();
    public ObservableCollection<string> FilteredWhitelist { get; } = new();

    // Commands
    public ICommand ClearLogsCommand { get; }
    public ICommand ExportLogsCommand { get; }
    public ICommand SaveServerAddressCommand { get; }
    public ICommand SaveUsernameCommand { get; }
    public ICommand RegisterCommand { get; }
    public ICommand ConnectCommand { get; }
    public ICommand DisconnectCommand { get; }
    public ICommand ChangeServerCommand { get; }

    // Event for auto-scroll notification
    public event Action? ScrollToBottomRequested;

    private bool _disposed;

    private async void OnStatusTimerElapsed(object? sender, ElapsedEventArgs e)
    {
        if (_disposed) return;
        try
        {
            var status = await _ipcService.GetStatusAsync().ConfigureAwait(false);
            if (_disposed) return;
            
            System.Windows.Application.Current.Dispatcher.BeginInvoke(() =>
            {
                if (_disposed) return;
                ClientConnectionStatus = status.IsConnected ? "Подключено" : "Отключено";
            });

            if (!status.IsConnected && SettingsAvailable)
            {
                System.Windows.Application.Current.Dispatcher.BeginInvoke(() =>
                {
                    if (_disposed) return;
                    SettingsAvailable = false;
                });
            }
            else if (status.IsConnected && !SettingsAvailable)
            {
                await LoadSettingsAsync();
            }
            
            if (status.WhitelistRevision != 0 && status.WhitelistRevision != _lastWhitelistRevision)
            {
                _lastWhitelistRevision = status.WhitelistRevision;
                
                var whitelist = await _ipcService.GetWhitelistAsync().ConfigureAwait(false);
                if (_disposed) return;
                
                if (whitelist.Count > 0)
                {
                    System.Windows.Application.Current.Dispatcher.BeginInvoke(() =>
                    {
                        if (_disposed) return;
                        WhitelistEntries.Clear();
                        foreach (var domain in whitelist)
                        {
                            WhitelistEntries.Add(WhitelistEntry.Parse(domain));
                        }
                        
                        UpdateWhitelistDisplay();
                        AddLog("INFO", $"Whitelist updated ({whitelist.Count} domains)");
                    });
                }
            }
        }
        catch
        {
            if (_disposed) return;
            System.Windows.Application.Current.Dispatcher.BeginInvoke(() =>
            {
                if (_disposed) return;
                ClientConnectionStatus = "Отключено";
            });
        }
    }

    public MainViewModel()
    {
        ClearLogsCommand = new RelayCommand(_ => ClearLogs());
        ExportLogsCommand = new RelayCommand(_ => ExportLogs());
        SaveServerAddressCommand = new RelayCommand(async _ => await SaveServerAddressAsync());
        SaveUsernameCommand = new RelayCommand(async _ => await SaveUsernameAsync());
        RegisterCommand = new RelayCommand(async _ => await RegisterAsync());
        ConnectCommand = new RelayCommand(async _ => await ConnectAsync());
        DisconnectCommand = new RelayCommand(async _ => await DisconnectAsync());
        ChangeServerCommand = new RelayCommand(async _ => await ChangeServerAsync());

        // Initialize whitelist status polling timer
        _statusTimer.Elapsed += OnStatusTimerElapsed;
        _statusTimer.AutoReset = true;
        _statusTimer.Enabled = true;

        AddLog("INFO", "Клиент запущен");

        // Load settings from backend
        LoadSettingsAsync();
    }

    private async Task LoadSettingsAsync()
    {
        var addr = await _ipcService.GetServerAddressAsync();
        var name = await _ipcService.GetUsernameAsync();
        var enabled = await _ipcService.GetFallbackWhitelistEnabledAsync();
        var srvName = await _ipcService.GetServerNameAsync();
        var available = addr != "" || name != "" || enabled || srvName != "";

        System.Windows.Application.Current.Dispatcher.BeginInvoke(() =>
        {
            _serverAddress = addr;
            OnPropertyChanged(nameof(ServerAddress));
            _username = name;
            OnPropertyChanged(nameof(Username));
            _serverName = srvName;
            OnPropertyChanged(nameof(ServerName));
            _fallbackWhitelistEnabled = enabled;
            OnPropertyChanged(nameof(FallbackWhitelistEnabled));
            SettingsAvailable = available;
        });
    }

    private async Task SaveServerAddressAsync()
    {
        var addr = ServerAddress?.Trim() ?? "";
        if (string.IsNullOrEmpty(addr)) return;
        var ok = await _ipcService.SetServerAddressAsync(addr);
        AddLog(ok ? "INFO" : "ERROR", ok ? $"Адрес сервера сохранён: {addr}" : "Ошибка сохранения адреса сервера");
    }

    private async Task SaveUsernameAsync()
    {
        var name = Username?.Trim() ?? "";
        if (string.IsNullOrEmpty(name)) return;
        var ok = await _ipcService.SetUsernameAsync(name);
        AddLog(ok ? "INFO" : "ERROR", ok ? $"Имя пользователя сохранено: {name}" : "Ошибка сохранения имени");
    }

    private async Task RegisterAsync()
    {
        var name = Username?.Trim() ?? "";
        if (string.IsNullOrEmpty(name))
        {
            AddLog("ERROR", "Укажите имя пользователя в настройках");
            return;
        }
        var ok = await _ipcService.RegisterAsync(name);
        AddLog(ok ? "INFO" : "ERROR", ok ? $"Запрос на регистрацию отправлен: {name}" : "Ошибка регистрации");
    }

    private async Task ConnectAsync()
    {
        var name = Username?.Trim() ?? "";
        if (string.IsNullOrEmpty(name))
        {
            AddLog("ERROR", "Укажите имя пользователя в настройках");
            return;
        }
        var ok = await _ipcService.ConnectToServerAsync(name);
        AddLog(ok ? "INFO" : "ERROR", ok ? $"Подключено к серверу: {name}" : "Ошибка подключения");
    }

    private async Task DisconnectAsync()
    {
        var name = Username?.Trim() ?? "";
        if (string.IsNullOrEmpty(name)) return;
        var ok = await _ipcService.DisconnectFromServerAsync(name);
        AddLog(ok ? "INFO" : "ERROR", ok ? "Отключено от сервера" : "Ошибка отключения");
    }

    private async Task ChangeServerAsync()
    {
        var servers = await _ipcService.DiscoverServersAsync();
        if (servers.Count == 0)
        {
            AddLog("ERROR", "Серверы не найдены в сети");
            return;
        }

        var list = servers.Select(s =>
        {
            var parts = s.Split('|');
            return new ServerInfo
            {
                Name = parts.Length > 0 ? parts[0] : "",
                Ip = parts.Length > 1 ? parts[1] : ""
            };
        }).ToList();

        ServerInfo? selected = null;
        System.Windows.Application.Current.Dispatcher.Invoke(() =>
        {
            var dialog = new ServerSelectWindow(list)
            {
                Owner = System.Windows.Application.Current.MainWindow
            };
            if (dialog.ShowDialog() == true)
            {
                selected = dialog.SelectedServer;
            }
        });

        if (selected == null || string.IsNullOrEmpty(selected.Ip)) return;

        await _ipcService.SetServerAddressAsync(selected.Ip);
        await _ipcService.SetServerNameAsync(selected.Name);

        System.Windows.Application.Current.Dispatcher.BeginInvoke(() =>
        {
            ServerAddress = selected.Ip;
            ServerName = selected.Name;
        });

        AddLog("INFO", $"Выбран сервер: {selected.Name} ({selected.Ip})");
    }

    private readonly ConcurrentQueue<string> _pendingLogs = new();
    
    public void AddLog(string level, string message)
    {
        _pendingLogs.Enqueue(message);
    }
    
    public void FlushPendingLogs()
    {
        var toAdd = new List<string>();
        while (_pendingLogs.TryDequeue(out var msg))
        {
            toAdd.Add(msg);
            if (toAdd.Count >= 50) break;
        }
        
        if (toAdd.Count == 0) return;
        
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

    public void Dispose()
    {
        if (_disposed) return;
        _disposed = true;
        _statusTimer?.Stop();
        _statusTimer?.Dispose();
    }
}
