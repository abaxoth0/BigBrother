using System.Collections.Concurrent;
using System.Collections.ObjectModel;
using System.IO;
using System.Timers;
using System.Windows;
using System.Windows.Input;
using frontend.Models;
using frontend.Services;

namespace frontend.ViewModels;

public enum HealthState
{
    Unknown,
    AllOk,
    Degraded,
    Critical
}

public class MainViewModel : ViewModelBase, IDisposable
{
    private const int MaxLogs = 500;

    // Services
    private readonly IpcService _ipcService = new IpcService();
    private readonly ServiceManager _serviceManager = new("BigBrother Firewall");
    private readonly LogReader _clientLogReader = new();
    private readonly LogReader _firewallLogReader = new();

    /// <summary>Shared pipe service used by the ViewModel and the main window.</summary>
    public IpcService IpcService => _ipcService;

    private int _lastClientPid;
    private string _lastClientLogPath = "";
    private string _lastFirewallLogPath = "";

    // Status
    private bool _filtrationEnabled = true;
    private bool _hasSettingsChanges;
    private bool _isRegistering;
    private bool _autoScroll = true;
    private int _whitelistCount = 0;
    private DateTime _lastUpdate = DateTime.Now;

    public bool AutoScroll
    {
        get => _autoScroll;
        set => SetProperty(ref _autoScroll, value);
    }

    private bool _firewallRunning = true;
    private bool _serverConnected;
    private HealthState _overallHealth = HealthState.Unknown;

    public bool FirewallRunning
    {
        get => _firewallRunning;
        set => SetProperty(ref _firewallRunning, value);
    }

    public bool ServerConnected
    {
        get => _serverConnected;
        set => SetProperty(ref _serverConnected, value);
    }

    public bool FiltrationEnabled
    {
        get => _filtrationEnabled;
        set => SetProperty(ref _filtrationEnabled, value);
    }

    public HealthState OverallHealth
    {
        get => _overallHealth;
        set
        {
            if (SetProperty(ref _overallHealth, value))
                OnPropertyChanged(nameof(OverallHealthText));
        }
    }

    public string OverallHealthText => OverallHealth switch
    {
        HealthState.AllOk => "Всё работает",
        HealthState.Degraded => "Работает с ограничениями",
        HealthState.Critical => "Клиент недоступен",
        _ => "Проверка статуса..."
    };

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

    // Log level filters
    private bool _showInfo = true;
    private bool _showErrors = true;
    private bool _showWarnings = true;
    private bool _showDns = true;
    private bool _showBlocked = true;

    public bool ShowInfo { get => _showInfo; set { if (SetProperty(ref _showInfo, value)) UpdateFilteredLogs(); } }
    public bool ShowErrors { get => _showErrors; set { if (SetProperty(ref _showErrors, value)) UpdateFilteredLogs(); } }
    public bool ShowWarnings { get => _showWarnings; set { if (SetProperty(ref _showWarnings, value)) UpdateFilteredLogs(); } }
    public bool ShowDns { get => _showDns; set { if (SetProperty(ref _showDns, value)) UpdateFilteredLogs(); } }
    public bool ShowBlocked { get => _showBlocked; set { if (SetProperty(ref _showBlocked, value)) UpdateFilteredLogs(); } }

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
            if (SetProperty(ref _fallbackWhitelistEnabled, value)) MarkSettingsChanged();
        }
    }

    private bool _filtrationAutoDisable;

    public bool FiltrationAutoDisable
    {
        get => _filtrationAutoDisable;
        set
        {
            if (SetProperty(ref _filtrationAutoDisable, value)) MarkSettingsChanged();
        }
    }

    public bool HasSettingsChanges
    {
        get => _hasSettingsChanges;
        set => SetProperty(ref _hasSettingsChanges, value);
    }

    public bool IsRegistering
    {
        get => _isRegistering;
        set
        {
            if (SetProperty(ref _isRegistering, value))
                OnPropertyChanged(nameof(CanRegister));
        }
    }

    public bool CanRegister => !_isRegistering;

    private void MarkSettingsChanged()
    {
        if (!_hasSettingsChanges)
            HasSettingsChanges = true;
    }

    private string _serverAddress = "";
    public string ServerAddress
    {
        get => _serverAddress;
        set
        {
            if (SetProperty(ref _serverAddress, value)) MarkSettingsChanged();
        }
    }

    private string _username = "";
    public string Username
    {
        get => _username;
        set
        {
            if (SetProperty(ref _username, value)) MarkSettingsChanged();
        }
    }

    private string _serverPort = "1984";
    public string ServerPort
    {
        get => _serverPort;
        set
        {
            if (SetProperty(ref _serverPort, value)) MarkSettingsChanged();
        }
    }

    private bool _discoveryEnabled = true;
    public bool DiscoveryEnabled
    {
        get => _discoveryEnabled;
        set
        {
            if (SetProperty(ref _discoveryEnabled, value))
            {
                MarkSettingsChanged();
                System.Windows.Input.CommandManager.InvalidateRequerySuggested();
            }
        }
    }

    private bool _networkAuto = true;
    public bool NetworkAuto
    {
        get => _networkAuto;
        set
        {
            if (SetProperty(ref _networkAuto, value))
            {
                MarkSettingsChanged();
                OnPropertyChanged(nameof(NetworkManualMode));
            }
        }
    }

    public bool NetworkManualMode => !_networkAuto;

    private string _networkGateway = "";
    public string NetworkGateway
    {
        get => _networkGateway;
        set
        {
            if (SetProperty(ref _networkGateway, value)) MarkSettingsChanged();
        }
    }

    private string _networkMask = "";
    public string NetworkMask
    {
        get => _networkMask;
        set
        {
            if (SetProperty(ref _networkMask, value)) MarkSettingsChanged();
        }
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
    private readonly System.Timers.Timer _statusTimer = new System.Timers.Timer(60000); // Safety fallback (events drive updates)
    private int _refreshInProgress;
    private int _refreshQueued;

    /// <summary>Raised after each status refresh with the latest daemon status.</summary>
    public event Action<ClientStatus>? StatusRefreshed;

    public ObservableCollection<WhitelistEntry> WhitelistEntries { get; } = new();
    public ObservableCollection<string> FilteredWhitelist { get; } = new();

    // Commands
    public ICommand ClearLogsCommand { get; }
    public ICommand ExportLogsCommand { get; }
    public ICommand RegisterCommand { get; }
    public ICommand ConnectCommand { get; }
    public ICommand DisconnectCommand { get; }
    public ICommand ChangeServerCommand { get; }
    public ICommand SaveSettingsCommand { get; }
    public ICommand ToggleFiltrationCommand { get; }
    public ICommand StartDaemonCommand { get; }
    public ICommand StopDaemonCommand { get; }
    public ICommand RestartDaemonCommand { get; }
    public ICommand RestartClientCommand { get; }
    public ICommand RefreshStatusCommand { get; }
    public ICommand OpenHistoryCommand { get; }
    public ICommand OpenArchivedLogCommand { get; }
    public ICommand JumpToBottomCommand { get; }

    // Event for auto-scroll notification
    public event Action? ScrollToBottomRequested;

    /// <summary>Raised when the user requests the log-history viewer with the given sources.</summary>
    public event Action<List<LogFileSource>>? LogViewRequested;

    private bool _disposed;

    private async void OnStatusTimerElapsed(object? sender, ElapsedEventArgs e)
    {
        if (_disposed) return;
        await RefreshStateAsync();
    }

    private void OnStateChanged()
    {
        if (_disposed) return;
        _ = RefreshStateAsync();
    }

    public async Task RefreshStateAsync()
    {
        // Single-flight: coalesce concurrent refreshes (event bursts + timer).
        if (Interlocked.CompareExchange(ref _refreshInProgress, 1, 0) != 0)
        {
            Interlocked.Exchange(ref _refreshQueued, 1);
            return;
        }

        try
        {
            do
            {
                Interlocked.Exchange(ref _refreshQueued, 0);
                try
                {
                    var status = await _ipcService.GetStatusAsync().ConfigureAwait(false);
                    if (_disposed) return;

                    System.Windows.Application.Current.Dispatcher.BeginInvoke(() =>
                    {
                        if (_disposed) return;
                        UpdateStatus(status);
                        LastUpdate = DateTime.Now;
                        StatusRefreshed?.Invoke(status);
                    });

                    UpdateLogReaders(status);

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
                        FirewallRunning = false;
                        ServerConnected = false;
                        OverallHealth = HealthState.Critical;
                        StatusRefreshed?.Invoke(new ClientStatus { ClientPid = 0 });
                    });
                    UpdateLogReaders(new ClientStatus { ClientPid = 0 });
                }
            } while (Interlocked.CompareExchange(ref _refreshQueued, 0, 1) == 1 && !_disposed);
        }
        finally
        {
            Interlocked.Exchange(ref _refreshInProgress, 0);
        }
    }

    public MainViewModel()
    {
        ClearLogsCommand = new RelayCommand(_ => ClearLogs());
        ExportLogsCommand = new RelayCommand(_ => ExportLogs());
        RegisterCommand = new RelayCommand(async _ => await RegisterAsync());
        ConnectCommand = new RelayCommand(async _ => await ConnectAsync());
        DisconnectCommand = new RelayCommand(async _ => await DisconnectAsync());
        ChangeServerCommand = new RelayCommand(async _ => await ChangeServerAsync(), _ => DiscoveryEnabled);
        SaveSettingsCommand = new RelayCommand(async _ => await SaveAllSettingsAsync());
        ToggleFiltrationCommand = new RelayCommand(async _ => await ToggleFiltrationAsync());
        StartDaemonCommand = new RelayCommand(async _ => await StartDaemonAsync());
        StopDaemonCommand = new RelayCommand(async _ => await StopDaemonAsync());
        RestartDaemonCommand = new RelayCommand(async _ => await RestartDaemonAsync());
        RestartClientCommand = new RelayCommand(async _ => await RestartClientAsync());
        RefreshStatusCommand = new RelayCommand(async _ => await RefreshStateAsync());
        OpenHistoryCommand = new RelayCommand(_ => OpenHistory());
        OpenArchivedLogCommand = new RelayCommand(_ => OpenArchivedLogs());
        JumpToBottomCommand = new RelayCommand(_ => ScrollToBottomRequested?.Invoke());

        // Initialize whitelist status polling timer (safety fallback; events drive updates)
        _statusTimer.Elapsed += OnStatusTimerElapsed;
        _statusTimer.AutoReset = true;
        _statusTimer.Enabled = true;

        // Event-driven updates from the daemon pipe
        _ipcService.StateChanged += OnStateChanged;
        _ipcService.StartEventSubscription();

        // Log UI flush timer (batches log lines onto the UI thread)
        var logFlushTimer = new System.Windows.Threading.DispatcherTimer
        {
            Interval = TimeSpan.FromMilliseconds(500)
        };
        logFlushTimer.Tick += (s, e) => FlushPendingLogs();
        logFlushTimer.Start();

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
        var port = await _ipcService.GetServerPortAsync();
        var discEnabled = await _ipcService.GetDiscoveryEnabledAsync();
        var netAuto = await _ipcService.GetNetworkAutoAsync();
        var netGw = await _ipcService.GetNetworkGatewayAsync();
        var netMask = await _ipcService.GetNetworkMaskAsync();
        var filtAuto = await _ipcService.GetFiltrationAutoDisableAsync();
        var available = addr != "" || name != "" || enabled || srvName != "";

        System.Windows.Application.Current.Dispatcher.BeginInvoke(() =>
        {
            _serverAddress = addr;
            OnPropertyChanged(nameof(ServerAddress));
            _username = name;
            OnPropertyChanged(nameof(Username));
            _serverName = srvName;
            OnPropertyChanged(nameof(ServerName));
            _serverPort = port;
            OnPropertyChanged(nameof(ServerPort));
            _discoveryEnabled = discEnabled;
            OnPropertyChanged(nameof(DiscoveryEnabled));
            _networkAuto = netAuto;
            OnPropertyChanged(nameof(NetworkAuto));
            _networkGateway = netGw;
            OnPropertyChanged(nameof(NetworkGateway));
            _networkMask = netMask;
            OnPropertyChanged(nameof(NetworkMask));
            _fallbackWhitelistEnabled = enabled;
            OnPropertyChanged(nameof(FallbackWhitelistEnabled));
            _filtrationAutoDisable = filtAuto;
            OnPropertyChanged(nameof(FiltrationAutoDisable));
            SettingsAvailable = available;
        });
    }

    private async Task SaveAllSettingsAsync()
    {
        await SaveFieldAsync("Адрес сервера", ServerAddress, s => _ipcService.SetServerAddressAsync(s));
        await SaveFieldAsync("Имя пользователя", Username, s => _ipcService.SetUsernameAsync(s));
        await SaveFieldAsync("Порт сервера", ServerPort, s => _ipcService.SetServerPortAsync(s));
        await SaveFieldAsync("Шлюз", NetworkGateway, s => _ipcService.SetNetworkGatewayAsync(s));
        await SaveFieldAsync("Маска подсети", NetworkMask, s => _ipcService.SetNetworkMaskAsync(s));
        await _ipcService.SetFallbackWhitelistEnabledAsync(FallbackWhitelistEnabled);
        await _ipcService.SetDiscoveryEnabledAsync(DiscoveryEnabled);
        await _ipcService.SetNetworkAutoAsync(NetworkAuto);
        await _ipcService.SetFiltrationAutoDisableAsync(FiltrationAutoDisable);
        HasSettingsChanges = false;
    }

    private async Task SaveFieldAsync(string label, string value, Func<string, Task<bool>> setter)
    {
        var v = value?.Trim() ?? "";
        if (string.IsNullOrEmpty(v)) return;
        var ok = await setter(v);
        AddLog(ok ? "INFO" : "ERROR", ok ? $"{label}: сохранено" : $"Ошибка сохранения: {label}");
    }

    private async Task ToggleFiltrationAsync()
    {
        var newState = !FiltrationEnabled;
        var ok = await _ipcService.SetFiltrationEnabledAsync(newState);
        AddLog(ok ? "INFO" : "ERROR", ok
            ? $"Фильтрация {(newState ? "включена" : "отключена")}"
            : "Ошибка переключения фильтрации");
        if (ok) FiltrationEnabled = newState;
    }

    private async Task RegisterAsync()
    {
        var name = Username?.Trim() ?? "";
        if (string.IsNullOrEmpty(name))
        {
            System.Windows.MessageBox.Show("Укажите имя пользователя в поле «Пользователь» и нажмите «Сохранить».", "Регистрация",
                System.Windows.MessageBoxButton.OK, System.Windows.MessageBoxImage.Warning);
            AddLog("ERROR", "Укажите имя пользователя в настройках");
            return;
        }

        IsRegistering = true;
        try
        {
            var ok = await _ipcService.RegisterAsync(name);
            AddLog(ok ? "INFO" : "ERROR", ok ? $"Запрос на регистрацию отправлен: {name}" : "Ошибка регистрации");
            if (ok)
            {
                System.Windows.MessageBox.Show("Запрос на регистрацию принят. Ожидайте подтверждения.",
                    "Регистрация", System.Windows.MessageBoxButton.OK, System.Windows.MessageBoxImage.Information);
            }
            else
            {
                System.Windows.MessageBox.Show("Ошибка регистрации.",
                    "Регистрация", System.Windows.MessageBoxButton.OK, System.Windows.MessageBoxImage.Error);
            }
        }
        finally
        {
            IsRegistering = false;
        }
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
                Ip = parts.Length > 1 ? parts[1] : "",
                Port = parts.Length > 2 ? parts[2] : "1984"
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
        await _ipcService.SetServerPortAsync(selected.Port);

        System.Windows.Application.Current.Dispatcher.BeginInvoke(() =>
        {
            ServerAddress = selected.Ip;
            ServerName = selected.Name;
            ServerPort = selected.Port;
        });

        AddLog("INFO", $"Выбран сервер: {selected.Name} ({selected.Ip}:{selected.Port})");
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
                // Keep the filtered mirror in sync when the source is trimmed.
                while (FilteredLogs.Count > Logs.Count)
                    FilteredLogs.RemoveAt(0);
            }

            var newEntries = new List<LogEntry>(toAdd.Count);
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
                newEntries.Add(entry);
            }

            if (!string.IsNullOrEmpty(LogSearchText))
            {
                UpdateFilteredLogs();
            }
            else
            {
                // Incremental: mirror only the new entries (avoid full rebuild).
                foreach (var entry in newEntries)
                {
                    if (IsLevelVisible(entry.Level))
                        FilteredLogs.Add(entry);
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

        var searchLower = LogSearchText?.ToLower() ?? "";
        foreach (var log in Logs)
        {
            if (!IsLevelVisible(log.Level)) continue;
            if (string.IsNullOrEmpty(searchLower) ||
                log.Message.ToLower().Contains(searchLower) ||
                log.Level.ToLower().Contains(searchLower))
            {
                FilteredLogs.Add(log);
            }
        }
    }

    private bool IsLevelVisible(string level)
    {
        return level switch
        {
            "ERROR" => ShowErrors,
            "WARNING" => ShowWarnings,
            "DNS" => ShowDns,
            "BLOCKED" => ShowBlocked,
            _ => ShowInfo
        };
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

    private void UpdateStatus(ClientStatus status)
    {
        var daemonRunning = status.DaemonStatus == "RUNNING";
        var serverConnected = status.IsServerSessionActive;
        FirewallRunning = daemonRunning;
        ServerConnected = serverConnected;
        FiltrationEnabled = status.FiltrationEnabled;

        if (!daemonRunning)
            OverallHealth = HealthState.Critical;
        else if (!serverConnected || !status.FiltrationEnabled)
            OverallHealth = HealthState.Degraded;
        else
            OverallHealth = HealthState.AllOk;
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

    private async Task StartDaemonAsync()
    {
        AddLog("INFO", "Запуск демона...");
        try
        {
            var result = await _serviceManager.StartServiceAsync();
            AddLog(result ? "INFO" : "ERROR", result ? "Демон запущен" : "Не удалось запустить демон");
            await Task.Delay(2000);
            await RefreshStateAsync();
        }
        catch (Exception ex)
        {
            AddLog("ERROR", $"Ошибка: {ex.Message}");
        }
    }

    private async Task StopDaemonAsync()
    {
        AddLog("INFO", "Остановка демона...");
        try
        {
            var result = await _serviceManager.StopServiceAsync();
            AddLog(result ? "INFO" : "ERROR", result ? "Демон остановлен" : "Не удалось остановить демон");
            await RefreshStateAsync();
        }
        catch (Exception ex)
        {
            AddLog("ERROR", $"Ошибка: {ex.Message}");
        }
    }

    private async Task RestartDaemonAsync()
    {
        AddLog("INFO", "Рестарт демона...");
        try
        {
            var result = await _serviceManager.RestartServiceAsync();
            AddLog(result ? "INFO" : "ERROR", result ? "Демон перезапущен" : "Не удалось перезапустить демон");
            await Task.Delay(3000);
            await RefreshStateAsync();
        }
        catch (Exception ex)
        {
            AddLog("ERROR", $"Ошибка: {ex.Message}");
        }
    }

    private async Task RestartClientAsync()
    {
        AddLog("INFO", "Перезапуск клиента...");
        var result = await _ipcService.RestartClientAsync();
        AddLog(result ? "INFO" : "ERROR", result ? "Клиент перезапущен" : "Не удалось перезапустить клиент");
        await Task.Delay(2000);
        await RefreshStateAsync();
    }

    // Log reader lifecycle: start/stop the binary log tailers as the client process appears.
    private void UpdateLogReaders(ClientStatus status)
    {
        if (status.ClientPid != 0 && _lastClientPid == 0)
        {
            _ = StartLogReadersAsync();
        }
        else if (status.ClientPid == 0 && _lastClientPid != 0)
        {
            _clientLogReader.Stop();
            _firewallLogReader.Stop();
        }
        _lastClientPid = status.ClientPid;
    }

    private async Task StartLogReadersAsync()
    {
        string clientLog = "";
        string firewallLog = "";

        try
        {
            var paths = await _ipcService.GetLogPathAsync();
            clientLog = paths.clientLog;
            firewallLog = paths.firewallLog;
        }
        catch { }

        // If IPC failed, try to use previous paths
        if (string.IsNullOrEmpty(clientLog))
            clientLog = _lastClientLogPath;
        if (string.IsNullOrEmpty(firewallLog))
            firewallLog = _lastFirewallLogPath;

        // Save for next time
        if (!string.IsNullOrEmpty(clientLog))
            _lastClientLogPath = clientLog;
        if (!string.IsNullOrEmpty(firewallLog))
            _lastFirewallLogPath = firewallLog;

        if (!string.IsNullOrEmpty(clientLog) && File.Exists(clientLog))
        {
            _clientLogReader.Stop();
            _clientLogReader.OnNewLine -= OnLogLineReceived;
            _clientLogReader.OnNewLine += OnLogLineReceived;
            _clientLogReader.Start(clientLog, Lib.LogSource.Client);
        }

        if (!string.IsNullOrEmpty(firewallLog) && File.Exists(firewallLog))
        {
            _firewallLogReader.Stop();
            _firewallLogReader.OnNewLine -= OnLogLineReceived;
            _firewallLogReader.OnNewLine += OnLogLineReceived;
            _firewallLogReader.Start(firewallLog, Lib.LogSource.Firewall);
        }

        if (!string.IsNullOrEmpty(clientLog) || !string.IsNullOrEmpty(firewallLog))
        {
            AddLog("INFO", $"Чтение логов: client={clientLog}, firewall={firewallLog}");
        }
    }

    private void OnLogLineReceived(string line)
    {
        // LogReader already dispatches to the UI thread before calling this.
        string level = "INFO";
        if (line.Contains("[ERROR]") || line.Contains("ERROR"))
            level = "ERROR";
        else if (line.Contains("[WARNING]") || line.Contains("[WARN]"))
            level = "WARNING";
        else if (line.Contains("[DNS]"))
            level = "DNS";
        else if (line.Contains("[BLOCKED]"))
            level = "BLOCKED";

        AddLog(level, line);
    }

    public void OpenHistory()
    {
        string clientLog = _lastClientLogPath;
        string firewallLog = _lastFirewallLogPath;

        // Only try IPC if we have a valid client PID (connected)
        if (_lastClientPid != 0)
        {
            try
            {
                var paths = _ipcService.GetLogPathAsync().GetAwaiter().GetResult();
                if (!string.IsNullOrEmpty(paths.clientLog))
                    clientLog = paths.clientLog;
                if (!string.IsNullOrEmpty(paths.firewallLog))
                    firewallLog = paths.firewallLog;
            }
            catch { }
        }

        // Save for next time
        if (!string.IsNullOrEmpty(clientLog))
            _lastClientLogPath = clientLog;
        if (!string.IsNullOrEmpty(firewallLog))
            _lastFirewallLogPath = firewallLog;

        if (string.IsNullOrEmpty(clientLog) && string.IsNullOrEmpty(firewallLog))
        {
            System.Windows.MessageBox.Show("Нет доступных логов", "Ошибка", MessageBoxButton.OK, MessageBoxImage.Warning);
            return;
        }

        var sources = new List<LogFileSource>();
        if (!string.IsNullOrEmpty(firewallLog))
        {
            sources.Add(new LogFileSource
            {
                Label = "Firewall",
                FilePath = firewallLog,
                Source = Lib.LogSource.Firewall
            });
        }
        if (!string.IsNullOrEmpty(clientLog))
        {
            sources.Add(new LogFileSource
            {
                Label = "Client",
                FilePath = clientLog,
                Source = Lib.LogSource.Client
            });
        }

        LogViewRequested?.Invoke(sources);
    }

    public void OpenArchivedLogs()
    {
        var dialog = new Microsoft.Win32.OpenFileDialog
        {
            Filter = "Binary log files (*.binlog)|*.binlog|All files (*.*)|*.*",
            Multiselect = true,
            Title = "Выберите файлы логов"
        };

        if (dialog.ShowDialog() != true || dialog.FileNames.Length == 0)
            return;

        var sources = new List<LogFileSource>();
        foreach (var path in dialog.FileNames)
        {
            var fileName = Path.GetFileNameWithoutExtension(path);
            var label = fileName.Contains("firewall") ? "Firewall"
                : fileName.Contains("client") ? "Client"
                : fileName;

            sources.Add(new LogFileSource
            {
                Label = label,
                FilePath = path,
                Source = Lib.LogSource.Unknown
            });
        }

        LogViewRequested?.Invoke(sources);
    }

    public void Dispose()
    {
        if (_disposed) return;
        _disposed = true;
        _statusTimer?.Stop();
        _statusTimer?.Dispose();
        _ipcService.StateChanged -= OnStateChanged;
        _ipcService.StopEventSubscription();
        _clientLogReader.Stop();
        _firewallLogReader.Stop();
        _ipcService.Dispose();
    }
}
