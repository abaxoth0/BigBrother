using System.Collections.ObjectModel;
using System.IO;
using System.Linq;
using System.Text.Json;
using System.Windows;
using System.Windows.Input;
using frontend.Services;
using frontend.Views;

namespace frontend.ViewModels;

public class RelayCommand : ICommand
{
    private readonly Action<object?> _execute;
    private readonly Predicate<object?>? _canExecute;

    public RelayCommand(Action<object?> execute, Predicate<object?>? canExecute = null)
    {
        _execute = execute ?? throw new ArgumentNullException(nameof(execute));
        _canExecute = canExecute;
    }

    public bool CanExecute(object? parameter) => _canExecute?.Invoke(parameter) ?? true;

    public void Execute(object? parameter) => _execute(parameter);

    public event EventHandler? CanExecuteChanged
    {
        add => CommandManager.RequerySuggested += value;
        remove => CommandManager.RequerySuggested -= value;
    }

    public void RaiseCanExecuteChanged()
    {
        CommandManager.InvalidateRequerySuggested();
    }
}

public class ServerLogEntry
{
    public string Timestamp { get; set; } = "";
    public string Level { get; set; } = "";
    public string Source { get; set; } = "";
    public string Message { get; set; } = "";
}

public class MainViewModel : ViewModelBase
{
    private readonly IpcService _ipcService;
    private readonly ServiceManager _serviceManager;
    private System.Timers.Timer? _refreshTimer;
    private System.Timers.Timer? _serviceStatusTimer;
    private System.Timers.Timer? _logReaderTimer;

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

    private string _logPath = "";
    private string _lastLogFile = "";
    private long _lastLogPosition;
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

        ConnectedClients = new ObservableCollection<ConnectedClient>();
        PendingRegistrations = new ObservableCollection<PendingRegistration>();
        Whitelists = new ObservableCollection<WhitelistInfo>();
        LogEntries = new ObservableCollection<string>();

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
        StartLogReader();
        _ = RefreshAllAsync();
        _ = LoadServerNameAsync();
        _ = LoadServerPortAsync();
        _ = LoadLogPathAsync();
    }

    public ObservableCollection<ConnectedClient> ConnectedClients { get; }
    public ObservableCollection<PendingRegistration> PendingRegistrations { get; }
    public ObservableCollection<WhitelistInfo> Whitelists { get; }
    public ObservableCollection<string> LogEntries { get; }

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
        _refreshTimer = new System.Timers.Timer(5000);
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
            _logPath = path;
        }
    }

    private void StartLogReader()
    {
        _logReaderTimer = new System.Timers.Timer(500);
        _logReaderTimer.Elapsed += (s, e) =>
        {
            if (_disposed) return;
            ReadServerLogs();
        };
        _logReaderTimer.Start();
    }

    private void ReadServerLogs()
    {
        if (string.IsNullOrEmpty(_logPath))
        {
            AddServerLogEntry("DEBUG", "Лог-путь не задан (IPC ещё не ответил)");
            return;
        }

        try
        {
            var dir = new DirectoryInfo(_logPath);
            if (!dir.Exists)
            {
                AddServerLogEntry("DEBUG", $"Директория логов не найдена: {_logPath}");
                return;
            }

            var files = dir.GetFiles("*.log");
            if (files.Length == 0)
            {
                AddServerLogEntry("DEBUG", $"Файлы *.log не найдены в {_logPath}");
                return;
            }

            var file = files.OrderByDescending(f => f.LastWriteTime).First();

            using var stream = new FileStream(file.FullName, FileMode.Open, FileAccess.Read, FileShare.ReadWrite);

            // New log file (e.g., after restart) — read from near the end
            if (_lastLogFile != file.FullName)
            {
                _lastLogFile = file.FullName;
                _lastLogPosition = Math.Max(0, stream.Length - 512 * 1024);
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
                System.Windows.Application.Current.Dispatcher.InvokeAsync(() =>
                {
                    foreach (var entry in newEntries)
                    {
                        _serverLogs.Add(entry);
                    }
                    while (_serverLogs.Count > 2000)
                        _serverLogs.RemoveAt(0);
                    FilterServerLogs();
                    if (_autoScroll) AutoScrollRequested?.Invoke();
                });
            }
        }
        catch { }
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

    private void AddServerLogEntry(string level, string message)
    {
        System.Windows.Application.Current.Dispatcher.InvokeAsync(() =>
        {
            _serverLogs.Add(new ServerLogEntry
            {
                Timestamp = DateTime.Now.ToString("HH:mm:ss"),
                Level = level,
                Message = message
            });
            FilterServerLogs();
            if (_autoScroll) AutoScrollRequested?.Invoke();
        });
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
        await Task.Run(async () =>
        {
            await RefreshServerStatusAsync();
            await RefreshClientsAsync();
            await RefreshPendingAsync();
            await RefreshWhitelistsAsync();
            await LoadActiveWhitelistAsync();
        });
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
                ConnectedClients.Clear();
                foreach (var client in clients)
                {
                    ConnectedClients.Add(client);
                }
                ConnectedClientsCount = clients.Count;
                AddLog("INFO", $"Клиентов: {clients.Count}");
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
                PendingRegistrations.Clear();
                foreach (var p in pending)
                {
                    PendingRegistrations.Add(p);
                }
                PendingCount = pending.Count;
                if (pending.Count > 0)
                    AddLog("INFO", $"Ожидают регистрации: {pending.Count}");
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
        var dialog = new WhitelistEditWindow();
        dialog.ViewModel.SetCreateMode();
        dialog.Owner = Application.Current.MainWindow;

        dialog.ViewModel.Saved += async (s, e) =>
        {
            var name = dialog.ViewModel.WhitelistName;
            var entries = dialog.ViewModel.GetEntries();
            var ok = await _ipcService.CreateWhitelistAsync(name);
            if (ok && entries.Count > 0)
            {
                ok = await _ipcService.SaveWhitelistAsync(name, entries);
            }
            AddLog(ok ? "INFO" : "ERROR", ok ? $"Список {name} создан" : $"Ошибка создания списка {name}");
            await RefreshWhitelistsAsync();
            if (ok) dialog.DialogResult = true;
        };

        dialog.ShowDialog();
    }

    private async Task OpenEditWhitelistAsync(WhitelistInfo? whitelist)
    {
        if (whitelist == null) return;

        var dialog = new WhitelistEditWindow();
        dialog.Owner = Application.Current.MainWindow;

        try
        {
            var entries = await _ipcService.GetWhitelistEntriesAsync(whitelist.Name);
            dialog.ViewModel.SetEditMode(whitelist.Name, entries);
        }
        catch
        {
            dialog.ViewModel.SetEditMode(whitelist.Name, new List<string>());
        }

        dialog.ViewModel.Saved += async (s, e) =>
        {
            var newName = dialog.ViewModel.WhitelistName;
            var entries = dialog.ViewModel.GetEntries();
            var ok = true;

            if (dialog.ViewModel.IsEditMode() && dialog.ViewModel.GetOriginalName() != newName)
            {
                ok = await _ipcService.RenameWhitelistAsync(dialog.ViewModel.GetOriginalName(), newName);
            }

            if (ok) ok = await _ipcService.SaveWhitelistAsync(newName, entries);
            AddLog(ok ? "INFO" : "ERROR", ok ? $"Список {newName} сохранен" : $"Ошибка сохранения списка {newName}");
            await RefreshWhitelistsAsync();
            if (ok) dialog.DialogResult = true;
        };

        dialog.ShowDialog();
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
            LogEntries.Add($"[{DateTime.Now:HH:mm:ss}] [{level}] {message}");
            if (LogEntries.Count > 1000)
            {
                LogEntries.RemoveAt(0);
            }
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
        _logReaderTimer?.Stop();
        _logReaderTimer?.Dispose();
        _ipcService.Dispose();
        _serviceManager.Dispose();
    }
}
