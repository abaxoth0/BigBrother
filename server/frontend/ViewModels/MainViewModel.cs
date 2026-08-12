using System.Collections.ObjectModel;
using System.IO;
using System.Linq;
using System.Text.Json;
using System.Windows;
using System.Windows.Input;
using frontend.Models;
using frontend.Services;

namespace frontend.ViewModels;

public class MainViewModel : ViewModelBase
{
    private readonly IpcService _ipcService;
    private readonly ServiceManager _serviceManager;
    private readonly ServerLogTailer _logTailer;
    private readonly IWhitelistDialogService _whitelistDialog;
    private System.Timers.Timer? _refreshTimer;
    private System.Timers.Timer? _serviceStatusTimer;
    private int _refreshInProgress;

    private string _serverStatus = "Подключение...";
    private string _uptime = "";
    private int _connectedClientsCount;
    private int _pendingCount;
    private bool _isConnected;
    private bool _hasSettingsChanges;

    private string _serviceStatus = "Проверка...";
    private string _filtrationStatus = "N/A";
    private string _activeWhitelist = "";
    private string _serverName = "";
    private string _serverPort = "1984";
    private bool _isAllSelected;
    private ObservableCollection<WhitelistInfo> _whitelists = new();
    private WhitelistInfo? _selectedWhitelist;

    private string _logSearchText = "";
    private bool _autoScroll = true;
    private ObservableCollection<ServerLogEntry> _serverLogs = new();
    private ObservableCollection<ServerLogEntry> _filteredServerLogs = new();

    public ObservableCollection<ServerLogEntry> ServerLogs => _serverLogs;
    public ObservableCollection<ServerLogEntry> FilteredServerLogs => _filteredServerLogs;

    public MainViewModel()
    {
        _ipcService = new IpcService();
        _serviceManager = new ServiceManager();
        _logTailer = new ServerLogTailer();
        _logTailer.EntriesRead += OnLogEntriesRead;
        _whitelistDialog = new WhitelistDialogService();

        ConnectedClients = new ObservableCollection<ConnectedClient>();
        PendingRegistrations = new ObservableCollection<PendingRegistration>();
        Whitelists = new ObservableCollection<WhitelistInfo>();

        ApproveCommand = new RelayCommand(async o => await ApproveUserAsync(o?.ToString()!), o => o != null);
        RejectCommand = new RelayCommand(async o => await RejectUserAsync(o?.ToString()!), o => o != null);
        DisconnectCommand = new RelayCommand(async o => await DisconnectUserAsync(o?.ToString()!), o => o != null);
        RefreshCommand = new RelayCommand(async _ => await RefreshAllAsync());
        DeleteWhitelistCommand = new RelayCommand(async o => await DeleteWhitelistAsync(o?.ToString()!), o => o != null);
        SetActiveWhitelistCommand = new RelayCommand(async o => await SetActiveWhitelistAsync(o?.ToString()!), o => o != null);
        OpenCreateWhitelistCommand = new RelayCommand(async _ => await OpenCreateWhitelistAsync());
        OpenEditWhitelistCommand = new RelayCommand(async o => await OpenEditWhitelistAsync(o as WhitelistInfo), o => o is WhitelistInfo);
        SaveServerNameCommand = new RelayCommand(async _ => await SaveServerNameAsync());
        SaveServerPortCommand = new RelayCommand(async _ => await SaveServerPortAsync());
        ImportWhitelistsCommand = new RelayCommand(async _ => await ImportWhitelistsAsync());
        ExportWhitelistsCommand = new RelayCommand(async _ => await ExportWhitelistsAsync(), _ => Whitelists.Any(w => w.IsSelected));
        DeleteSelectedWhitelistsCommand = new RelayCommand(async _ => await DeleteSelectedWhitelistsAsync(), _ => Whitelists.Any(w => w.IsSelected));
        SaveServerSettingsCommand = new RelayCommand(async _ => await SaveAllServerSettingsAsync());
        ToggleFiltrationCommand = new RelayCommand(async _ => await ToggleFiltrationAsync());
        StartServiceCommand = new RelayCommand(async _ => await StartServiceAsync());
        StopServiceCommand = new RelayCommand(async _ => await StopServiceAsync());
        RestartServiceCommand = new RelayCommand(async _ => await RestartServiceAsync());
        ClearServerLogsCommand = new RelayCommand(_ => ClearServerLogs());
        ExportServerLogsCommand = new RelayCommand(_ => ExportServerLogs());
        StartAutoRefresh();
        StartServiceStatusPolling();
        _ipcService.EventReceived += OnEventReceived;
        _ipcService.Resubscribed += OnEventResubscribed;
        _ipcService.StartEventSubscription();
        _ = RefreshAllAsync();
        _ = LoadServerNameAsync();
        _ = LoadServerPortAsync();
        _ = LoadLogPathAsync();
    }

    // Event-driven refresh: the server pushes state changes over SUBSCRIBE.
    private void OnEventReceived(string type, Dictionary<string, string> data)
    {
        if (_disposed) return;
        switch (type)
        {
            case "USER_CONNECTED":
            case "USER_DISCONNECTED":
                _ = RefreshClientsAsync();
                _ = RefreshServerStatusAsync();
                break;
            case "PENDING_ADDED":
            case "PENDING_REMOVED":
            case "USER_APPROVED":
            case "USER_REJECTED":
                _ = RefreshPendingAsync();
                _ = RefreshServerStatusAsync();
                break;
            case "WHITELIST_CHANGED":
                _ = RefreshWhitelistsAsync();
                _ = LoadActiveWhitelistAsync();
                break;
            case "FILTRATION_TOGGLED":
                _ = RefreshServerStatusAsync();
                break;
        }
    }

    private void OnEventResubscribed()
    {
        if (_disposed) return;
        _ = RefreshAllAsync();
    }

    public ObservableCollection<ConnectedClient> ConnectedClients { get; }
    public ObservableCollection<PendingRegistration> PendingRegistrations { get; }
    public ObservableCollection<WhitelistInfo> Whitelists { get; }

    public string ServerStatus
    {
        get => _serverStatus;
        set => SetProperty(ref _serverStatus, value);
    }

    public string Uptime
    {
        get => _uptime;
        set => SetProperty(ref _uptime, value);
    }

    public int ConnectedClientsCount
    {
        get => _connectedClientsCount;
        set => SetProperty(ref _connectedClientsCount, value);
    }

    public int PendingCount
    {
        get => _pendingCount;
        set => SetProperty(ref _pendingCount, value);
    }

    public bool IsConnected
    {
        get => _isConnected;
        set
        {
            if (SetProperty(ref _isConnected, value))
            {
                OnPropertyChanged(nameof(NotConnectedVisibility));
                OnPropertyChanged(nameof(ConnectedVisibility));
            }
        }
    }

    public Visibility NotConnectedVisibility => IsConnected ? Visibility.Collapsed : Visibility.Visible;
    public Visibility ConnectedVisibility => IsConnected ? Visibility.Visible : Visibility.Collapsed;

    public string FiltrationStatus
    {
        get => _filtrationStatus;
        set => SetProperty(ref _filtrationStatus, value);
    }

    public string ServiceStatus
    {
        get => _serviceStatus;
        set => SetProperty(ref _serviceStatus, value);
    }

    public string ActiveWhitelist
    {
        get => _activeWhitelist;
        set => SetProperty(ref _activeWhitelist, value);
    }

    public string ServerName
    {
        get => _serverName;
        set
        {
            if (SetProperty(ref _serverName, value)) MarkSettingsChanged();
        }
    }

    public string ServerPort
    {
        get => _serverPort;
        set
        {
            if (SetProperty(ref _serverPort, value)) MarkSettingsChanged();
        }
    }

    public bool HasSettingsChanges
    {
        get => _hasSettingsChanges;
        set => SetProperty(ref _hasSettingsChanges, value);
    }

    private void MarkSettingsChanged()
    {
        if (!_hasSettingsChanges)
            HasSettingsChanges = true;
    }

    public WhitelistInfo? SelectedWhitelist
    {
        get => _selectedWhitelist;
        set
        {
            if (SetProperty(ref _selectedWhitelist, value))
            {
                OnPropertyChanged(nameof(SelectedWhitelistName));
                OnPropertyChanged(nameof(HasSelectedWhitelist));
            }
        }
    }

    public string SelectedWhitelistName => SelectedWhitelist?.Name ?? "";
    public bool HasSelectedWhitelist => SelectedWhitelist != null;

    public bool IsAllSelected
    {
        get => _isAllSelected;
        set
        {
            if (SetProperty(ref _isAllSelected, value))
            {
                foreach (var wl in Whitelists)
                    wl.IsSelected = value;
                CommandManager.InvalidateRequerySuggested();
            }
        }
    }

    public string LogSearchText
    {
        get => _logSearchText;
        set
        {
            if (SetProperty(ref _logSearchText, value))
                FilterServerLogs();
        }
    }

    public bool AutoScroll
    {
        get => _autoScroll;
        set => SetProperty(ref _autoScroll, value);
    }

    public ICommand ApproveCommand { get; }
    public ICommand RejectCommand { get; }
    public ICommand DisconnectCommand { get; }
    public ICommand RefreshCommand { get; }
    public ICommand DeleteWhitelistCommand { get; }
    public ICommand SetActiveWhitelistCommand { get; }
    public ICommand OpenCreateWhitelistCommand { get; }
    public ICommand OpenEditWhitelistCommand { get; }
    public ICommand SaveServerNameCommand { get; }
    public ICommand SaveServerPortCommand { get; }
    public ICommand ImportWhitelistsCommand { get; }
    public ICommand ExportWhitelistsCommand { get; }
    public ICommand DeleteSelectedWhitelistsCommand { get; }
    public ICommand SaveServerSettingsCommand { get; }
    public ICommand ToggleFiltrationCommand { get; }
    public ICommand StartServiceCommand { get; }
    public ICommand StopServiceCommand { get; }
    public ICommand RestartServiceCommand { get; }
    public ICommand ClearServerLogsCommand { get; }
    public ICommand ExportServerLogsCommand { get; }

    public event Action? AutoScrollRequested;
    private void StartAutoRefresh()
    {
        // Safety net only: updates are event-driven via the server SUBSCRIBE feed.
        _refreshTimer = new System.Timers.Timer(30000);
        _refreshTimer.Elapsed += async (s, e) =>
        {
            if (_disposed) return;
            try { await RefreshAllAsync(); } catch { }
        };
        _refreshTimer.Start();
    }

    private void StartServiceStatusPolling()
    {
        _serviceStatusTimer = new System.Timers.Timer(2000);
        _serviceStatusTimer.Elapsed += (s, e) =>
        {
            if (_disposed) return;
            try
            {
                var info = _serviceManager.GetServiceInfo();
                var status = info?.Status.ToString() ?? "Not Found";
                System.Windows.Application.Current.Dispatcher.InvokeAsync(() =>
                {
                    ServiceStatus = status;
                });
            }
            catch { }
        };
        _serviceStatusTimer.Start();
    }

    private async Task StartServiceAsync()
    {
        AddLog("INFO", "Запуск службы...");
        var ok = await _serviceManager.StartServiceAsync();
        AddLog(ok ? "INFO" : "ERROR", ok ? "Служба запущена" : "Ошибка запуска службы");
        if (ok)
        {
            await LoadLogPathAsync();
            await RefreshAllAsync();
        }
    }

    private async Task StopServiceAsync()
    {
        AddLog("INFO", "Остановка службы...");
        var ok = await _serviceManager.StopServiceAsync();
        AddLog(ok ? "INFO" : "ERROR", ok ? "Служба остановлена" : "Ошибка остановки службы");
    }

    private async Task RestartServiceAsync()
    {
        AddLog("INFO", "Перезапуск службы...");
        var ok = await _serviceManager.RestartServiceAsync();
        AddLog(ok ? "INFO" : "ERROR", ok ? "Служба перезапущена" : "Ошибка перезапуска службы");
    }

    private async Task ToggleFiltrationAsync()
    {
        try
        {
            var newState = FiltrationStatus == "Вкл" ? "0" : "1";
            AddLog("DEBUG", $"ToggleFiltration: current='{FiltrationStatus}', newState='{newState}'");
            var ok = await _ipcService.SetFiltrationEnabledAsync(newState == "1");
            AddLog(ok ? "INFO" : "ERROR", ok
                ? $"Фильтрация {(newState == "1" ? "включена" : "отключена")}"
                : "Ошибка переключения фильтрации");
            await RefreshServerStatusAsync();
        }
        catch (Exception ex)
        {
            AddLog("ERROR", $"Ошибка: {ex.Message}");
        }
    }

    private async Task LoadLogPathAsync()
    {
        var path = await _ipcService.GetLogPathAsync();
        if (!string.IsNullOrEmpty(path))
        {
            _logTailer.SetLogPath(path);
        }
    }

    private void OnLogEntriesRead(List<ServerLogEntry> newEntries)
    {
        if (_disposed) return;
        System.Windows.Application.Current.Dispatcher.InvokeAsync(() =>
        {
            if (_disposed) return;
            foreach (var entry in newEntries)
            {
                _serverLogs.Add(entry);
            }
            while (_serverLogs.Count > 2000)
                _serverLogs.RemoveAt(0);

            if (string.IsNullOrEmpty(_logSearchText))
            {
                // Incremental: mirror only the new entries (avoid full rebuild).
                foreach (var entry in newEntries)
                {
                    _filteredServerLogs.Add(entry);
                }
                while (_filteredServerLogs.Count > _serverLogs.Count)
                    _filteredServerLogs.RemoveAt(0);
            }
            else
            {
                FilterServerLogs();
            }
            if (_autoScroll) AutoScrollRequested?.Invoke();
        });
    }

    private void FilterServerLogs()
    {
        if (string.IsNullOrEmpty(_logSearchText))
        {
            _filteredServerLogs.Clear();
            foreach (var e in _serverLogs)
                _filteredServerLogs.Add(e);
        }
        else
        {
            var filtered = _serverLogs
                .Where(e => e.Message.Contains(_logSearchText, StringComparison.OrdinalIgnoreCase)
                         || e.Level.Contains(_logSearchText, StringComparison.OrdinalIgnoreCase)
                         || e.Source.Contains(_logSearchText, StringComparison.OrdinalIgnoreCase))
                .ToList();
            _filteredServerLogs.Clear();
            foreach (var e in filtered)
                _filteredServerLogs.Add(e);
        }
    }

    private void ClearServerLogs()
    {
        _serverLogs.Clear();
        _filteredServerLogs.Clear();
    }

    private void ExportServerLogs()
    {
        var dialog = new Microsoft.Win32.SaveFileDialog
        {
            Title = "Экспорт логов сервера",
            Filter = "Text files (*.txt)|*.txt",
            FileName = $"server-logs-{DateTime.Now:yyyy-MM-dd}.txt"
        };

        if (dialog.ShowDialog() != true) return;

        try
        {
            var lines = _filteredServerLogs.Select(e =>
                $"[{e.Timestamp}] [{e.Level}]{(string.IsNullOrEmpty(e.Source) ? "" : $" [{e.Source}]")} {e.Message}");
            File.WriteAllLines(dialog.FileName, lines);
            AddLog("INFO", $"Логи сервера экспортированы: {dialog.FileName}");
        }
        catch (Exception ex)
        {
            AddLog("ERROR", $"Ошибка экспорта: {ex.Message}");
        }
    }

    public async Task RefreshAllAsync()
    {
        // Single-flight: coalesce overlapping refresh chains (timer + manual refresh)
        // so the IPC retry storm doesn't pile up when the backend is starting.
        if (Interlocked.CompareExchange(ref _refreshInProgress, 1, 0) != 0)
            return;
        try
        {
            await Task.Run(async () =>
            {
                await RefreshServerStatusAsync();
                await RefreshClientsAsync();
                await RefreshPendingAsync();
                await RefreshWhitelistsAsync();
                await LoadActiveWhitelistAsync();
            });
        }
        finally
        {
            Interlocked.Exchange(ref _refreshInProgress, 0);
        }
    }

    private async Task LoadActiveWhitelistAsync()
    {
        var wl = await _ipcService.GetActiveWhitelistAsync();
        await System.Windows.Application.Current.Dispatcher.InvokeAsync(() =>
        {
            ActiveWhitelist = wl;
        });
    }

    private async Task LoadServerNameAsync()
    {
        var name = await _ipcService.GetServerNameAsync();
        await System.Windows.Application.Current.Dispatcher.InvokeAsync(() =>
        {
            ServerName = name;
        });
    }

    private async Task LoadServerPortAsync()
    {
        var port = await _ipcService.GetServerPortAsync();
        await System.Windows.Application.Current.Dispatcher.InvokeAsync(() =>
        {
            ServerPort = port;
        });
    }

    private async Task SaveServerNameAsync()
    {
        var name = ServerName?.Trim() ?? "";
        if (string.IsNullOrEmpty(name)) return;
        if (await _ipcService.SetServerNameAsync(name))
            AddLog("INFO", $"Имя сервера сохранено: {name}");
        else
            AddLog("ERROR", "Ошибка сохранения имени сервера");
    }

    private async Task SaveServerPortAsync()
    {
        var port = ServerPort?.Trim() ?? "";
        if (string.IsNullOrEmpty(port)) return;
        if (await _ipcService.SetServerPortAsync(port))
            AddLog("INFO", $"Порт сервера сохранён: {port}");
        else
            AddLog("ERROR", "Ошибка сохранения порта сервера");
    }

    private async Task SaveAllServerSettingsAsync()
    {
        await SaveServerNameAsync();
        await SaveServerPortAsync();
        HasSettingsChanges = false;
    }

    private async Task RefreshServerStatusAsync()
    {
        try
        {
            var serverStatus = await _ipcService.GetServerStatusAsync();
            await System.Windows.Application.Current.Dispatcher.InvokeAsync(() =>
            {
                IsConnected = serverStatus.IsRunning;
                ServerStatus = serverStatus.IsRunning ? "Запущен" : "Не подключен";
                Uptime = serverStatus.IsRunning && !string.IsNullOrEmpty(serverStatus.UptimeText)
                    ? $"Аптайм: {serverStatus.UptimeText}" : "";
                ConnectedClientsCount = serverStatus.ConnectedClients;
                PendingCount = serverStatus.PendingCount;
                FiltrationStatus = serverStatus.IsRunning
                    ? (serverStatus.FiltrationEnabled ? "Вкл" : "Выкл")
                    : "N/A";
            });
        }
        catch (Exception ex)
        {
            if (_disposed) return;
            try
            {
                await System.Windows.Application.Current.Dispatcher.InvokeAsync(() =>
                {
                    if (_disposed) return;
                    IsConnected = false;
                    ServerStatus = "Не подключен";
                    Uptime = "";
                    ConnectedClients.Clear();
                    PendingRegistrations.Clear();
                    AddLog("ERROR", $"Ошибка: {ex.Message}");
                });
            }
            catch { }
        }
    }

    public async Task RefreshClientsAsync()
    {
        try
        {
            var clients = await _ipcService.GetClientsAsync();
            await System.Windows.Application.Current.Dispatcher.InvokeAsync(() =>
            {
                // Update in place to keep DataGrid rows stable (preserves context menu focus)
                var toRemove = ConnectedClients.Where(c => !clients.Any(n => n.Name == c.Name)).ToList();
                foreach (var c in toRemove)
                    ConnectedClients.Remove(c);

                foreach (var nc in clients)
                {
                    var existing = ConnectedClients.FirstOrDefault(c => c.Name == nc.Name);
                    if (existing != null)
                    {
                        existing.Address = nc.Address;
                        existing.Status = nc.Status;
                        existing.Whitelist = nc.Whitelist;
                        existing.LastActivity = nc.LastActivity;
                    }
                    else
                    {
                        ConnectedClients.Add(nc);
                    }
                }
                ConnectedClientsCount = ConnectedClients.Count;
                AddLog("INFO", $"Клиентов: {ConnectedClients.Count}");
            });
        }
        catch (Exception ex)
        {
            await System.Windows.Application.Current.Dispatcher.InvokeAsync(() =>
            {
                AddLog("ERROR", $"Ошибка загрузки клиентов: {ex.Message}");
            });
        }
    }

    public async Task RefreshPendingAsync()
    {
        try
        {
            var pending = await _ipcService.GetPendingAsync();
            await System.Windows.Application.Current.Dispatcher.InvokeAsync(() =>
            {
                // Update in place to keep DataGrid rows stable (preserves context menu focus)
                var toRemove = PendingRegistrations.Where(p => !pending.Any(n => n.Name == p.Name)).ToList();
                foreach (var p in toRemove)
                    PendingRegistrations.Remove(p);

                foreach (var np in pending)
                {
                    var existing = PendingRegistrations.FirstOrDefault(p => p.Name == np.Name);
                    if (existing != null)
                    {
                        existing.Address = np.Address;
                        existing.CreatedAt = np.CreatedAt;
                    }
                    else
                    {
                        PendingRegistrations.Add(np);
                    }
                }
                PendingCount = PendingRegistrations.Count;
                if (PendingRegistrations.Count > 0)
                    AddLog("INFO", $"Ожидают регистрации: {PendingRegistrations.Count}");
            });
        }
        catch (Exception ex)
        {
            await System.Windows.Application.Current.Dispatcher.InvokeAsync(() =>
            {
                AddLog("ERROR", $"Ошибка загрузки ожидающих: {ex.Message}");
            });
        }
    }

    public async Task RefreshWhitelistsAsync()
    {
        try
        {
            var whitelists = await _ipcService.GetWhitelistsAsync();
            await System.Windows.Application.Current.Dispatcher.InvokeAsync(() =>
            {
                // Update collection in-place to preserve DataGrid stability (context menus, selection, etc.)
                var toRemove = Whitelists.Where(w => !whitelists.Any(n => n.Name == w.Name)).ToList();
                foreach (var w in toRemove)
                    Whitelists.Remove(w);

                foreach (var nw in whitelists)
                {
                    var existing = Whitelists.FirstOrDefault(w => w.Name == nw.Name);
                    if (existing != null)
                    {
                        existing.EntryCount = nw.EntryCount;
                    }
                    else
                    {
                        SubscribeWhitelistSelection(nw);
                        Whitelists.Add(nw);
                    }
                }
                UpdateAllSelectedState();
                AddLog("INFO", $"Списков: {whitelists.Count}");
            });
        }
        catch (Exception ex)
        {
            await System.Windows.Application.Current.Dispatcher.InvokeAsync(() =>
            {
                if (!IsConnected)
                {
                    Whitelists.Clear();
                    ActiveWhitelist = "";
                }
                AddLog("ERROR", $"Ошибка загрузки списков: {ex.Message}");
            });
        }
    }

    private async Task ApproveUserAsync(string? name)
    {
        if (string.IsNullOrEmpty(name)) return;
        var ok = await _ipcService.ApproveUserAsync(name);
        AddLog(ok ? "INFO" : "ERROR", ok ? $"Пользователь {name} одобрен" : $"Ошибка одобрения {name}");
        await RefreshPendingAsync();
        await RefreshClientsAsync();
    }

    private async Task RejectUserAsync(string? name)
    {
        if (string.IsNullOrEmpty(name)) return;
        var ok = await _ipcService.RejectUserAsync(name);
        AddLog(ok ? "INFO" : "ERROR", ok ? $"Пользователь {name} отклонен" : $"Ошибка отклонения {name}");
        await RefreshPendingAsync();
    }

    private async Task DisconnectUserAsync(string? name)
    {
        if (string.IsNullOrEmpty(name)) return;
        var ok = await _ipcService.DisconnectUserAsync(name);
        AddLog(ok ? "INFO" : "ERROR", ok ? $"Пользователь {name} отключен" : $"Ошибка отключения {name}");
        await RefreshClientsAsync();
    }

    private async Task DeleteWhitelistAsync(string? name)
    {
        if (string.IsNullOrEmpty(name)) return;
        var ok = await _ipcService.DeleteWhitelistAsync(name);
        AddLog(ok ? "INFO" : "ERROR", ok ? $"Список {name} удален" : $"Ошибка удаления {name}");
        await RefreshWhitelistsAsync();
    }

    private async Task DeleteSelectedWhitelistsAsync()
    {
        var selected = Whitelists.Where(w => w.IsSelected).Select(w => w.Name).ToList();
        if (selected.Count == 0) return;

        var result = MessageBox.Show($"Удалить выбранные списки ({selected.Count})?",
            "Удаление списков", MessageBoxButton.YesNo, MessageBoxImage.Question);
        if (result != MessageBoxResult.Yes) return;

        var deleted = 0;
        var errors = 0;
        foreach (var name in selected)
        {
            if (await _ipcService.DeleteWhitelistAsync(name))
                deleted++;
            else
                errors++;
        }

        AddLog(deleted > 0 ? "INFO" : "ERROR",
            $"Удалено списков: {deleted}" + (errors > 0 ? $", ошибок: {errors}" : ""));
        IsAllSelected = false;
        await RefreshWhitelistsAsync();
    }

    private async Task SetActiveWhitelistAsync(string? name)
    {
        if (string.IsNullOrEmpty(name)) return;
        ActiveWhitelist = name;
        var ok = await _ipcService.SetActiveWhitelistAsync(name);
        AddLog(ok ? "INFO" : "ERROR", ok ? $"Активный список: {name}" : $"Ошибка установки активного списка");
    }

    private async Task OpenCreateWhitelistAsync()
    {
        var result = await _whitelistDialog.ShowDialogAsync(WhitelistDialogMode.Create);
        if (result == null) return; // cancelled

        var ok = await _ipcService.CreateWhitelistAsync(result.Name);
        if (ok && result.Entries.Count > 0)
        {
            ok = await _ipcService.SaveWhitelistAsync(result.Name, result.Entries);
        }
        AddLog(ok ? "INFO" : "ERROR", ok ? $"Список {result.Name} создан" : $"Ошибка создания списка {result.Name}");
        await RefreshWhitelistsAsync();
    }

    private async Task OpenEditWhitelistAsync(WhitelistInfo? whitelist)
    {
        if (whitelist == null) return;

        List<string> initialEntries;
        try
        {
            initialEntries = await _ipcService.GetWhitelistEntriesAsync(whitelist.Name);
        }
        catch
        {
            initialEntries = new List<string>();
        }

        var result = await _whitelistDialog.ShowDialogAsync(WhitelistDialogMode.Edit, whitelist.Name, initialEntries);
        if (result == null) return; // cancelled

        var ok = true;
        if (whitelist.Name != result.Name)
        {
            ok = await _ipcService.RenameWhitelistAsync(whitelist.Name, result.Name);
        }

        if (ok) ok = await _ipcService.SaveWhitelistAsync(result.Name, result.Entries);
        AddLog(ok ? "INFO" : "ERROR", ok ? $"Список {result.Name} сохранен" : $"Ошибка сохранения списка {result.Name}");
        await RefreshWhitelistsAsync();
    }

    private async Task ImportWhitelistsAsync()
    {
        var dialog = new Microsoft.Win32.OpenFileDialog
        {
            Title = "Импорт списков",
            Filter = "Whitelist files (*.wl)|*.wl|Text files (*.txt)|*.txt|All files (*.*)|*.*",
            Multiselect = true
        };

        if (dialog.ShowDialog() != true) return;

        var existingNames = new HashSet<string>(Whitelists.Select(w => w.Name));
        var imported = new List<string>();
        var errors = new List<string>();

        foreach (var filePath in dialog.FileNames)
        {
            try
            {
                var lines = File.ReadAllLines(filePath);
                string? currentName = null;
                var currentEntries = new List<string>();

                foreach (var rawLine in lines)
                {
                    var line = rawLine.Trim();
                    if (line.Length == 0 || line.StartsWith('#'))
                    {
                        if (line.StartsWith("# Whitelist:", StringComparison.OrdinalIgnoreCase))
                        {
                            if (currentName != null)
                            {
                                await SaveWhitelistFromImport(currentName, currentEntries, existingNames, imported, errors);
                            }
                            currentName = line.Substring("# Whitelist:".Length).Trim();
                            currentEntries = new List<string>();
                        }
                        continue;
                    }
                    currentEntries.Add(line);
                }

                if (currentName != null)
                {
                    await SaveWhitelistFromImport(currentName, currentEntries, existingNames, imported, errors);
                }
            }
            catch (Exception ex)
            {
                errors.Add($"{Path.GetFileName(filePath)}: {ex.Message}");
            }
        }

        await RefreshWhitelistsAsync();
        await LoadActiveWhitelistAsync();

        var msg = imported.Count > 0
            ? $"Импортировано списков: {imported.Count} ({string.Join(", ", imported)})"
            : "Не импортировано ни одного списка";
        if (errors.Count > 0)
            msg += $"\nОшибок: {errors.Count}";
        AddLog(imported.Count > 0 ? "INFO" : "WARNING", msg);

        if (errors.Count > 0)
        {
            MessageBox.Show(
                $"Импортировано: {imported.Count}\nОшибок: {errors.Count}\n\n{string.Join("\n", errors)}",
                "Импорт списков",
                MessageBoxButton.OK,
                errors.Count > 0 ? MessageBoxImage.Warning : MessageBoxImage.Information);
        }
    }

    private void SubscribeWhitelistSelection(WhitelistInfo wl)
    {
        wl.PropertyChanged += (s, e) =>
        {
            if (e.PropertyName == nameof(WhitelistInfo.IsSelected))
            {
                CommandManager.InvalidateRequerySuggested();
            }
        };
    }

    private void UpdateAllSelectedState()
    {
        _isAllSelected = Whitelists.Count > 0 && Whitelists.All(w => w.IsSelected);
        OnPropertyChanged(nameof(IsAllSelected));
    }

    private async Task SaveWhitelistFromImport(string name, List<string> entries, HashSet<string> existingNames, List<string> imported, List<string> errors)
    {
        try
        {
            if (existingNames.Contains(name))
            {
                errors.Add($"{name}: список с таким именем уже существует");
                return;
            }
            var ok = await _ipcService.CreateWhitelistAsync(name);
            if (ok && entries.Count > 0)
            {
                ok = await _ipcService.SaveWhitelistAsync(name, entries);
            }
            if (ok)
            {
                imported.Add(name);
                existingNames.Add(name);
            }
            else
                errors.Add($"{name}: не удалось создать список");
        }
        catch (Exception ex)
        {
            errors.Add($"{name}: {ex.Message}");
        }
    }

    private async Task ExportWhitelistsAsync()
    {
        var selected = Whitelists.Where(w => w.IsSelected).ToList();
        if (selected.Count == 0)
        {
            selected = Whitelists.ToList();
        }

        if (selected.Count == 0)
        {
            AddLog("WARNING", "Нет списков для экспорта");
            return;
        }

        var dialog = new Microsoft.Win32.SaveFileDialog
        {
            Title = "Экспорт списков",
            Filter = "Whitelist files (*.wl)|*.wl|Text files (*.txt)|*.txt",
            FileName = "whitelists.wl"
        };

        if (dialog.ShowDialog() != true) return;

        try
        {
            var lines = new List<string>();

            foreach (var wl in selected)
            {
                lines.Add($"# Whitelist: {wl.Name}");
                var entries = await _ipcService.GetWhitelistEntriesAsync(wl.Name);
                foreach (var entry in entries)
                {
                    lines.Add(entry);
                }
                lines.Add("");
            }

            File.WriteAllLines(dialog.FileName, lines);
            AddLog("INFO", $"Экспортировано списков: {selected.Count} в {dialog.FileName}");
        }
        catch (Exception ex)
        {
            AddLog("ERROR", $"Ошибка экспорта: {ex.Message}");
            MessageBox.Show($"Ошибка экспорта: {ex.Message}", "Экспорт списков",
                MessageBoxButton.OK, MessageBoxImage.Error);
        }
    }

    private void AddLog(string level, string message)
    {
        System.Windows.Application.Current.Dispatcher.BeginInvoke(() =>
        {
            _serverLogs.Add(new ServerLogEntry
            {
                Timestamp = DateTime.Now.ToString("HH:mm:ss"),
                Level = level,
                Source = "UI",
                Message = message
            });
            while (_serverLogs.Count > 2000)
                _serverLogs.RemoveAt(0);
            if (string.IsNullOrEmpty(_logSearchText))
            {
                _filteredServerLogs.Add(_serverLogs[^1]);
                while (_filteredServerLogs.Count > _serverLogs.Count)
                    _filteredServerLogs.RemoveAt(0);
            }
            else
            {
                FilterServerLogs();
            }
            if (_autoScroll) AutoScrollRequested?.Invoke();
        });
    }

    private bool _disposed;

    public void Dispose()
    {
        if (_disposed) return;
        _disposed = true;
        _refreshTimer?.Stop();
        _refreshTimer?.Dispose();
        _serviceStatusTimer?.Stop();
        _serviceStatusTimer?.Dispose();
        _ipcService.EventReceived -= OnEventReceived;
        _ipcService.Resubscribed -= OnEventResubscribed;
        _ipcService.StopEventSubscription();
        _logTailer.EntriesRead -= OnLogEntriesRead;
        _logTailer.Dispose();
        _ipcService.Dispose();
        _serviceManager.Dispose();
    }
}
