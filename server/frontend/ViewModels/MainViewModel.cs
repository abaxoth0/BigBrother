using System.Collections.ObjectModel;
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

public class MainViewModel : ViewModelBase
{
    private readonly IpcService _ipcService;
    private System.Timers.Timer? _refreshTimer;

    private string _serverStatus = "Подключение...";
    private string _uptime = "";
    private int _connectedClientsCount;
    private int _pendingCount;
    private bool _isConnected;

    private string _activeWhitelist = "";
    private string _serverName = "";
    private string _serverPort = "1984";
    private ObservableCollection<WhitelistInfo> _whitelists = new();
    private WhitelistInfo? _selectedWhitelist;

    public MainViewModel()
    {
        _ipcService = new IpcService();

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
        StartAutoRefresh();
        _ = RefreshAllAsync();
        _ = LoadServerNameAsync();
        _ = LoadServerPortAsync();
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

    public string ActiveWhitelist
    {
        get => _activeWhitelist;
        set => SetProperty(ref _activeWhitelist, value);
    }

    public string ServerName
    {
        get => _serverName;
        set => SetProperty(ref _serverName, value);
    }

    public string ServerPort
    {
        get => _serverPort;
        set => SetProperty(ref _serverPort, value);
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
                        Whitelists.Add(nw);
                    }
                }
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
        _ipcService.Dispose();
    }
}
