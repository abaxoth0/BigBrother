using System.Collections.ObjectModel;
using System.ComponentModel;
using System.IO;
using System.Linq;
using System.Text.Json;
using System.Security.Cryptography;
using System.Text.Json.Nodes;
using System.Windows;
using System.Windows.Input;
using System.Windows.Data;
using System.Globalization;
using frontend.Models;
using frontend.Services;
using frontend.Validation;
using frontend.Views;

namespace frontend.ViewModels;

public enum NavigationSection
{
    Main,
    Users,
    Lists,
    Settings
}

public enum UsersFilterMode
{
    Connected,
    Pending,
    All
}

public class MainViewModel : ViewModelBase
{
    private readonly IpcService _ipcService;
    private readonly ServiceManager _serviceManager;
    private readonly ServerLogTailer _logTailer;
    private readonly DispatcherLogQueue<ServerLogEntry> _logUpdates;
    private readonly IWhitelistDialogService _whitelistDialog;
    private System.Timers.Timer? _refreshTimer;
    private System.Timers.Timer? _serviceStatusTimer;
    private int _refreshInProgress;
    private int _statusRefreshInProgress;
    private bool _openingDomainCheck;
    private bool _openingWhitelistEditor;
    private bool _openingTimerEditor;
    private bool _openingPresetEditor;
    private readonly System.Windows.Threading.DispatcherTimer _countdownTimer = new();

    private string _serverStatus = "Подключение...";
    private string _uptime = "";
    private int _connectedClientsCount;
    private int _pendingCount;
    private bool _isConnected;
    private bool _hasSettingsChanges;

    private string _serviceStatus = "Проверка...";
    private string _filtrationStatus = "N/A";
    private readonly WhitelistActivationDraft _whitelistActivation = new();
    private bool _savingWhitelistActivation;
    private bool _syncingWhitelists;
    private int _whitelistActivationRevision;
    private List<WhitelistConflict> _whitelistConflicts = new();
    private string _serverName = "";
    private string _serverPort = "1984";
    private bool _isAllSelected;

    private string _logSearchText = "";
    private bool _autoScroll = true;
    private ObservableCollection<ServerLogEntry> _serverLogs = new();
    private ObservableCollection<ServerLogEntry> _filteredServerLogs = new();

    public ObservableCollection<ServerLogEntry> ServerLogs => _serverLogs;
    public ObservableCollection<ServerLogEntry> FilteredServerLogs => _filteredServerLogs;

    private NavigationSection _selectedSection = NavigationSection.Main;
    public NavigationSection SelectedSection
    {
        get => _selectedSection;
        set => SetProperty(ref _selectedSection, value);
    }

    private UsersFilterMode _usersFilter = UsersFilterMode.Connected;
    public UsersFilterMode UsersFilter
    {
        get => _usersFilter;
        set
        {
            if (SetProperty(ref _usersFilter, value))
            {
                OnPropertyChanged(nameof(UsersShowConnected));
                OnPropertyChanged(nameof(UsersShowPending));
                OnPropertyChanged(nameof(UsersShowAll));
                OnPropertyChanged(nameof(UsersFilterLabel));
                RebuildUsersView();
            }
        }
    }

    public bool UsersShowConnected => UsersFilter == UsersFilterMode.Connected;
    public bool UsersShowPending => UsersFilter == UsersFilterMode.Pending;
    public bool UsersShowAll => UsersFilter == UsersFilterMode.All;
    public string UsersFilterLabel => UsersFilter switch
    {
        UsersFilterMode.Pending => "Ожидающие регистрации",
        UsersFilterMode.All => "Все пользователи",
        _ => "Подключённые клиенты"
    };

    public int UsersFilterIndex
    {
        get => (int)UsersFilter;
        set => UsersFilter = value is >= 0 and <= 2 ? (UsersFilterMode)value : UsersFilterMode.Connected;
    }

    public ObservableCollection<ConnectedClient> FilteredConnected { get; } = new();
    public ObservableCollection<PendingRegistration> FilteredPending { get; } = new();
    public ObservableCollection<ConnectedClient> RegisteredUsers { get; } = new();
    public ObservableCollection<UserOverview> AllUsers { get; } = new();
    public ObservableCollection<UserOverview> FilteredAllUsers { get; } = new();

    private string _usersSearchText = "";
    public string UsersSearchText
    {
        get => _usersSearchText;
        set
        {
            if (SetProperty(ref _usersSearchText, value))
                RebuildUsersView();
        }
    }

    private DateTime _lastUpdate = DateTime.Now;
    public DateTime LastUpdate
    {
        get => _lastUpdate;
        set
        {
            if (SetProperty(ref _lastUpdate, value))
                OnPropertyChanged(nameof(LastUpdateFormatted));
        }
    }
    public string LastUpdateFormatted => $"Обновлено: {LastUpdate:HH:mm:ss}";

    // Log-level filters
    private bool _showInfo = true;
    private bool _showDebug = true;
    private bool _showTrace = true;
    private bool _showWarning = true;
    private bool _showError = true;
    private bool _showFatal = true;

    public bool ShowInfo { get => _showInfo; set { if (SetProperty(ref _showInfo, value)) FilterServerLogs(); } }
    public bool ShowDebug { get => _showDebug; set { if (SetProperty(ref _showDebug, value)) FilterServerLogs(); } }
    public bool ShowTrace { get => _showTrace; set { if (SetProperty(ref _showTrace, value)) FilterServerLogs(); } }
    public bool ShowWarning { get => _showWarning; set { if (SetProperty(ref _showWarning, value)) FilterServerLogs(); } }
    public bool ShowError { get => _showError; set { if (SetProperty(ref _showError, value)) FilterServerLogs(); } }
    public bool ShowFatal { get => _showFatal; set { if (SetProperty(ref _showFatal, value)) FilterServerLogs(); } }

    public ICommand JumpToBottomCommand { get; }

    public MainViewModel()
    {
        _ipcService = new IpcService();
        _serviceManager = new ServiceManager("BigBrother Server");
        _logUpdates = new DispatcherLogQueue<ServerLogEntry>(2000,
            System.Windows.Application.Current.Dispatcher, AppendLogEntries);
        _logTailer = new ServerLogTailer();
        _logTailer.EntriesRead += OnLogEntriesRead;
        _whitelistDialog = new WhitelistDialogService();

        ConnectedClients = new ObservableCollection<ConnectedClient>();
        PendingRegistrations = new ObservableCollection<PendingRegistration>();
        Whitelists = new ObservableCollection<WhitelistInfo>();
        RegularWhitelists = new ListCollectionView(Whitelists) { Filter = row => row is WhitelistInfo { IsTemporary: false } };
        TemporaryWhitelists = new ListCollectionView(Whitelists) { Filter = row => row is WhitelistInfo { IsTemporary: true } };
        _countdownTimer.Interval = TimeSpan.FromSeconds(1);
        _countdownTimer.Tick += (_, _) => { foreach (var row in Whitelists.Where(w => w.IsTemporary)) row.TickTimer(); };
        _countdownTimer.Start();

        ApproveCommand = new RelayCommand(async o => await RunUserActionAsync(() => ApproveUserAsync(o?.ToString())),
            o => CanRunUserAction && GetPendingTargets(o?.ToString()).Count > 0);
        RejectCommand = new RelayCommand(async o => await RunUserActionAsync(() => RejectUserAsync(o?.ToString())),
            o => CanRunUserAction && GetPendingTargets(o?.ToString()).Count > 0);
        ShowUserTokenCommand = new RelayCommand(async o => await RunUserActionAsync(() => ShowUserTokenAsync(o?.ToString())),
            o => CanRunUserAction && GetClientTargets(o?.ToString()).Count > 0);
        RegenerateTokenCommand = new RelayCommand(async o => await RunUserActionAsync(() => RegenerateUserTokenAsync(o?.ToString())),
            o => CanRunUserAction && GetClientTargets(o?.ToString()).Count > 0);
        DisconnectCommand = new RelayCommand(async o => await RunUserActionAsync(() => DisconnectUserAsync(o?.ToString())),
            o => CanRunUserAction && GetConnectedTargets(o?.ToString()).Count > 0);
        DisableClientIpv6BlockCommand = new RelayCommand(async o => await RunUserActionAsync(() => SetClientIpv6BlockAsync(o as ConnectedClient, false)),
            o => CanSetClientIpv6Block(o as ConnectedClient));
        EnableClientIpv6BlockCommand = new RelayCommand(async o => await RunUserActionAsync(() => SetClientIpv6BlockAsync(o as ConnectedClient, true)),
            o => CanSetClientIpv6Block(o as ConnectedClient));
        RefreshCommand = new RelayCommand(async _ => await RefreshAllAsync());
        DisableWhitelistsCommand = new RelayCommand(o => SetWhitelistsActive(o as WhitelistInfo, false),
            o => (CurrentWhitelists.Any(w => w.IsSelected) || o is WhitelistInfo) && IsConnected && !_savingWhitelistActivation);
        EnableWhitelistsCommand = new RelayCommand(o => SetWhitelistsActive(o as WhitelistInfo, true),
            o => (CurrentWhitelists.Any(w => w.IsSelected) || o is WhitelistInfo) && IsConnected && !_savingWhitelistActivation);
        SaveWhitelistActivationCommand = new RelayCommand(async _ => await SaveWhitelistActivationAsync(),
            _ => IsConnected && HasWhitelistActivationChanges && !_savingWhitelistActivation);
        CancelWhitelistActivationCommand = new RelayCommand(_ => CancelWhitelistActivation(),
            _ => HasWhitelistActivationChanges && !_savingWhitelistActivation);
        SaveWhitelistPresetCommand = new RelayCommand(async _ => await SaveWhitelistPresetAsync(), _ => CanUseWhitelistPresets);
        LoadWhitelistPresetCommand = new RelayCommand(async preset => await LoadWhitelistPresetAsync((WhitelistPreset)preset!),
            preset => preset is WhitelistPreset && CanUseWhitelistPresets);
        SyncWhitelistsCommand = new RelayCommand(async _ => await SyncWhitelistsAsync(),
            _ => IsConnected && !_syncingWhitelists && !_savingWhitelistActivation);
        OpenDomainCheckCommand = new RelayCommand(async _ => await OpenDomainCheckAsync(),
            _ => IsConnected && !_openingDomainCheck);
        EditWhitelistTimerCommand = new RelayCommand(o => OpenWhitelistTimer(o as WhitelistInfo),
            o => o is WhitelistInfo { IsTemporary: true } && IsConnected && !_openingTimerEditor);
        ResetWhitelistTimersCommand = new RelayCommand(async o => await ResetWhitelistTimersAsync(o as WhitelistInfo),
            o => (CurrentWhitelists.Any(w => w.IsSelected) || o is WhitelistInfo) && IsConnected && ShowTemporaryWhitelists);
        OpenCreateWhitelistCommand = new RelayCommand(async _ => await OpenCreateWhitelistAsync(), _ => !_openingWhitelistEditor);
        OpenEditWhitelistCommand = new RelayCommand(async o => await OpenEditWhitelistAsync(o as WhitelistInfo), o => o is WhitelistInfo && !_openingWhitelistEditor);
        ImportWhitelistsCommand = new RelayCommand(async _ => await ImportWhitelistsAsync());
        ExportWhitelistsCommand = new RelayCommand(async _ => await ExportWhitelistsAsync(), _ => CurrentWhitelists.Any(w => w.IsSelected));
        DeleteSelectedWhitelistsCommand = new RelayCommand(async o => await DeleteSelectedWhitelistsAsync(o as WhitelistInfo),
            o => CurrentWhitelists.Any(w => w.IsSelected) || o is WhitelistInfo);
        BrowseClientUpdateCommand = new RelayCommand(async _ => await BrowseClientUpdateAsync());
        OpenUpdateShareCommand = new RelayCommand(_ => OpenUpdateShare());
        PublishClientUpdateCommand = new RelayCommand(async _ => await PublishClientUpdateAsync(), _ => IsConnected);
        ClearClientUpdateCommand = new RelayCommand(async _ => await ClearClientUpdateAsync(), _ => IsConnected);
        SaveServerSettingsCommand = new RelayCommand(async _ => await SaveAllServerSettingsAsync());
        CopyCertificateFingerprintCommand = new RelayCommand(_ => Clipboard.SetText(CertificateFingerprint), _ => IsConnected && CertificateFingerprint.Length == 64);
        ToggleFiltrationCommand = new RelayCommand(async _ => await ToggleFiltrationAsync());
        StartServiceCommand = new RelayCommand(async _ => await StartServiceAsync());
        StopServiceCommand = new RelayCommand(async _ => await StopServiceAsync());
        RestartServiceCommand = new RelayCommand(async _ => await RestartServiceAsync());
        ClearServerLogsCommand = new RelayCommand(_ => ClearServerLogs());
        ExportServerLogsCommand = new RelayCommand(_ => ExportServerLogs());
        JumpToBottomCommand = new RelayCommand(_ => AutoScrollRequested?.Invoke());
        StartAutoRefresh();
        StartServiceStatusPolling();
        _ipcService.EventReceived += OnEventReceived;
        _ipcService.Resubscribed += OnEventResubscribed;
        _ipcService.StartEventSubscription();
        _ = RefreshAllAsync();
        _ = LoadServerNameAsync();
        _ = LoadServerPortAsync();
        _ = LoadWindowsNotificationsAsync();
        _ = LoadTemporaryWhitelistSettingsAsync();
        _ = LoadCertificateFingerprintAsync();
        _ = LoadClientUpdateAsync();
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
            case "CLIENT_POLICY_CHANGED":
                _ = RefreshClientsAsync();
                break;
            case "PENDING_ADDED":
            case "PENDING_REMOVED":
            case "USER_APPROVED":
            case "USER_REJECTED":
                _ = RefreshPendingAsync();
                if (type == "USER_APPROVED") _ = RefreshClientsAsync();
                _ = RefreshServerStatusAsync();
                break;
            case "WHITELIST_CHANGED":
                _ = RefreshWhitelistsAsync();
                break;
            case "CLIENT_UPDATE_CHANGED":
                _ = LoadClientUpdateAsync();
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
        _ = LoadCertificateFingerprintAsync();
        _ = LoadClientUpdateAsync();
    }

    public ObservableCollection<ConnectedClient> ConnectedClients { get; }
    public ObservableCollection<PendingRegistration> PendingRegistrations { get; }
    public ObservableCollection<WhitelistInfo> Whitelists { get; }
    public ICollectionView RegularWhitelists { get; }
    public ICollectionView TemporaryWhitelists { get; }
    public IEnumerable<WhitelistInfo> CurrentWhitelists => Whitelists.Where(w => w.IsTemporary == ShowTemporaryWhitelists);
    private int _whitelistTableIndex;
    public int WhitelistTableIndex
    {
        get => _whitelistTableIndex;
        set
        {
            if (!SetProperty(ref _whitelistTableIndex, value)) return;
            OnPropertyChanged(nameof(ShowTemporaryWhitelists));
            OnPropertyChanged(nameof(ShowRegularWhitelists));
            UpdateAllSelectedState();
            CommandManager.InvalidateRequerySuggested();
        }
    }
    public bool ShowTemporaryWhitelists => WhitelistTableIndex == 1;
    public bool ShowRegularWhitelists => !ShowTemporaryWhitelists;

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
        set
        {
            if (SetProperty(ref _connectedClientsCount, value))
                OnPropertyChanged(nameof(UsersCounterText));
        }
    }

    public int PendingCount
    {
        get => _pendingCount;
        set
        {
            if (SetProperty(ref _pendingCount, value))
                OnPropertyChanged(nameof(UsersCounterText));
        }
    }

    public string UsersCounterText => $"Пользователи (подключено / ожидают): {ConnectedClientsCount} / {PendingCount}";

    public bool IsConnected
    {
        get => _isConnected;
        set
        {
            if (SetProperty(ref _isConnected, value))
            {
                OnPropertyChanged(nameof(NotConnectedVisibility));
                OnPropertyChanged(nameof(ConnectedVisibility));
                OnPropertyChanged(nameof(CanUseWhitelistPresets));
                CommandManager.InvalidateRequerySuggested();
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

    public bool HasWhitelistActivationChanges => _whitelistActivation.HasChanges;
    public bool CanUseWhitelistPresets => IsConnected && !_savingWhitelistActivation && !_openingPresetEditor;
    public string ActiveWhitelistsText => $"Активных списков: {Whitelists.Count(w => w.IsActive && !w.IsTemporary)}";

    public string ActiveTemporaryWhitelistsText => $"Активных временных списков: {Whitelists.Count(w => w.IsActive && w.IsTemporary)}";

    public List<WhitelistConflict> WhitelistConflicts
    {
        get => _whitelistConflicts;
        private set
        {
            if (SetProperty(ref _whitelistConflicts, value))
                OnPropertyChanged(nameof(WhitelistConflictsText));
        }
    }

    public string WhitelistConflictsText =>
        (HasWhitelistActivationChanges ? "Показаны конфликты сохранённого списка. " : "") +
        (WhitelistConflicts.Count == 0 ? "Конфликтов нет"
            : $"Конфликты: {WhitelistConflicts.Count}. Побеждает больший приоритет; при равенстве — исключение.");

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

    private bool _saveTemporaryWhitelistState;
    public bool SaveTemporaryWhitelistState
    {
        get => _saveTemporaryWhitelistState;
        set { if (SetProperty(ref _saveTemporaryWhitelistState, value)) MarkSettingsChanged(); }
    }

    private bool _windowsNotificationsEnabled = true;
    public bool WindowsNotificationsEnabled
    {
        get => _windowsNotificationsEnabled;
        set
        {
            if (SetProperty(ref _windowsNotificationsEnabled, value)) MarkSettingsChanged();
        }
    }

    private void MarkSettingsChanged()
    {
        if (!_hasSettingsChanges)
            HasSettingsChanges = true;
    }

    public bool IsAllSelected
    {
        get => _isAllSelected;
        set
        {
            foreach (var wl in CurrentWhitelists)
                wl.IsSelected = value;
            UpdateAllSelectedState();
            CommandManager.InvalidateRequerySuggested();
        }
    }

    public bool AreAllConnectedSelected
    {
        get => ConnectedClients.Count > 0 && ConnectedClients.All(c => c.IsSelected);
        set { foreach (var client in ConnectedClients) client.IsSelected = value; }
    }

    public bool AreAllPendingSelected
    {
        get => PendingRegistrations.Count > 0 && PendingRegistrations.All(p => p.IsSelected);
        set { foreach (var pending in PendingRegistrations) pending.IsSelected = value; }
    }

    public bool AreAllUsersSelected
    {
        get => AllUsers.Count > 0 && AllUsers.All(u => u.IsSelected);
        set { foreach (var user in AllUsers) user.IsSelected = value; }
    }

    private void OnUserSelectionChanged(object? sender, PropertyChangedEventArgs e)
    {
        if (e.PropertyName == nameof(ISelectableRow.IsSelected)) NotifyUserSelectionChanged();
    }

    private void NotifyUserSelectionChanged()
    {
        OnPropertyChanged(nameof(AreAllConnectedSelected));
        OnPropertyChanged(nameof(AreAllPendingSelected));
        OnPropertyChanged(nameof(AreAllUsersSelected));
        CommandManager.InvalidateRequerySuggested();
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
    public ICommand ShowUserTokenCommand { get; }
    public ICommand RegenerateTokenCommand { get; }
    public ICommand RejectCommand { get; }
    public ICommand DisconnectCommand { get; }
    public ICommand DisableClientIpv6BlockCommand { get; }
    public ICommand EnableClientIpv6BlockCommand { get; }
    private bool _userActionInProgress;
    private bool CanRunUserAction => IsConnected && !_userActionInProgress;
    public ICommand RefreshCommand { get; }
    public ICommand DisableWhitelistsCommand { get; }
    public ICommand EnableWhitelistsCommand { get; }
    public ICommand SaveWhitelistActivationCommand { get; }
    public ICommand CancelWhitelistActivationCommand { get; }
    public ICommand SaveWhitelistPresetCommand { get; }
    public ICommand LoadWhitelistPresetCommand { get; }
    public ICommand SyncWhitelistsCommand { get; }
    public ICommand OpenDomainCheckCommand { get; }
    public ICommand EditWhitelistTimerCommand { get; }
    public ICommand ResetWhitelistTimersCommand { get; }
    public ICommand OpenCreateWhitelistCommand { get; }
    public ICommand OpenEditWhitelistCommand { get; }
    public ICommand ImportWhitelistsCommand { get; }
    public ICommand ExportWhitelistsCommand { get; }
    public ICommand DeleteSelectedWhitelistsCommand { get; }
    private string _clientUpdateLocation = "";
    private string _clientUpdateStatus = "Обновление не опубликовано";
    public string ClientUpdateLocation { get => _clientUpdateLocation; set => SetProperty(ref _clientUpdateLocation, value); }
    public string ClientUpdateStatus { get => _clientUpdateStatus; set => SetProperty(ref _clientUpdateStatus, value); }
    public ICommand BrowseClientUpdateCommand { get; }
    public ICommand OpenUpdateShareCommand { get; }
    public ICommand PublishClientUpdateCommand { get; }

    private async Task BrowseClientUpdateAsync()
    {
        try
        {
            var dialog = new Microsoft.Win32.OpenFileDialog {
                Title = "Выберите пакет обновления клиента",
                Filter = "Пакет обновления (*.exe)|*.exe",
                CheckFileExists = true,
                Multiselect = false
            };
            foreach (var folder in NetworkShareAccess.SavedLocations())
                dialog.CustomPlaces.Add(new Microsoft.Win32.FileDialogCustomPlace(folder));
            if (!string.IsNullOrWhiteSpace(ClientUpdateLocation))
            {
                var folder = Path.GetDirectoryName(NetworkShareAccess.ToUnc(ClientUpdateLocation))!;
                await NetworkShareAccess.ReadAsync(folder, () => {
                    using var entries = Directory.EnumerateFileSystemEntries(folder).GetEnumerator();
                    entries.MoveNext();
                    return true;
                }, NetworkShareDialog.ConnectAsync);
                dialog.InitialDirectory = folder;
            }
            if (dialog.ShowDialog() == true) ClientUpdateLocation = NetworkShareAccess.ToUnc(dialog.FileName);
        }
        catch (Exception ex) { ClientUpdateStatus = ex.Message; }
    }

    private void OpenUpdateShare()
    {
        try
        {
            var dialog = new NetworkShareWindow(ClientUpdateLocation) { Owner = Application.Current.MainWindow };
            if (dialog.ShowDialog() == true) ClientUpdateLocation = dialog.SelectedFile!;
        }
        catch (Exception ex) { ClientUpdateStatus = ex.Message; }
    }
    public ICommand ClearClientUpdateCommand { get; }

    private async Task LoadClientUpdateAsync()
    {
        try
        {
            var data = await _ipcService.ClientUpdateAsync();
            var manifest = string.IsNullOrEmpty(data) ? null : JsonNode.Parse(data);
            ClientUpdateLocation = manifest?["location"]?.GetValue<string>() ?? "";
            ClientUpdateStatus = manifest == null ? "Обновление не опубликовано" : "Опубликована версия " + manifest["version"]?.GetValue<string>();
        }
        catch (Exception ex) { ClientUpdateStatus = ex.Message; }
    }

    private async Task PublishClientUpdateAsync()
    {
        try
        {
            var location = NetworkShareAccess.ToUnc(ClientUpdateLocation);
            ClientUpdateLocation = location;
            if (!location.EndsWith(".exe", StringComparison.OrdinalIgnoreCase))
                throw new IOException("Укажите сетевой путь к файлу обновления.");
            ClientUpdateStatus = "Проверка пакета…";
            var metadata = await NetworkShareAccess.ReadAsync(location, () => {
                var manifest = JsonNode.Parse(File.ReadAllText(Path.ChangeExtension(location, ".manifest.json")))
                    ?? throw new IOException("Отсутствует манифест обновления.");
                using var package = File.OpenRead(location);
                if (package.Length != manifest["size"]?.GetValue<long>() ||
                    Convert.ToHexStringLower(SHA256.HashData(package)) != manifest["sha256"]?.GetValue<string>())
                    throw new IOException("Размер или SHA-256 пакета не совпадает с манифестом.");
                manifest["location"] = location;
                return manifest.ToJsonString(new JsonSerializerOptions { WriteIndented = false, Encoder = System.Text.Encodings.Web.JavaScriptEncoder.UnsafeRelaxedJsonEscaping });
            }, NetworkShareDialog.ConnectAsync);
            await _ipcService.ClientUpdateAsync(metadata);
            await LoadClientUpdateAsync();
        }
        catch (Exception ex) { ClientUpdateStatus = "Ошибка публикации: " + ex.Message; }
    }

    private async Task ClearClientUpdateAsync()
    {
        try { await _ipcService.ClientUpdateAsync(""); await LoadClientUpdateAsync(); }
        catch (Exception ex) { ClientUpdateStatus = ex.Message; }
    }

    public ICommand SaveServerSettingsCommand { get; }
    public ICommand CopyCertificateFingerprintCommand { get; }
    public ICommand ToggleFiltrationCommand { get; }
    public ICommand StartServiceCommand { get; }
    public ICommand StopServiceCommand { get; }
    public ICommand RestartServiceCommand { get; }
    public ICommand ClearServerLogsCommand { get; }
    public ICommand ExportServerLogsCommand { get; }

    public event Action? AutoScrollRequested;
    private void StartAutoRefresh()
    {
        _refreshTimer = new System.Timers.Timer(10000);
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
        _serviceStatusTimer.Elapsed += async (s, e) =>
        {
            if (_disposed) return;
            try
            {
                var info = _serviceManager.GetServiceInfo();
                var status = info?.Status.ToString() ?? "Not Found";
                await System.Windows.Application.Current.Dispatcher.InvokeAsync(() =>
                {
                    if (!_disposed) ServiceStatus = status;
                });
                await RefreshServerStatusAsync();
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
#if DEBUG
            AddLog("DEBUG", $"ToggleFiltration: current='{FiltrationStatus}', newState='{newState}'");
#endif
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

    private void OnLogEntriesRead(List<ServerLogEntry> entries)
    {
        if (_disposed) return;
        foreach (var entry in entries) _logUpdates.Add(entry);
    }

    private void AppendLogEntries(List<ServerLogEntry> entries)
    {
        if (_disposed) return;
        int removeCount = Math.Max(0, _serverLogs.Count + entries.Count - 2000);
        for (int i = 0; i < removeCount; i++)
        {
            var removed = _serverLogs[0];
            _serverLogs.RemoveAt(0);
            _filteredServerLogs.Remove(removed);
        }
        foreach (var entry in entries)
        {
            _serverLogs.Add(entry);
            if (IsLevelVisible(entry.Level) &&
                (string.IsNullOrEmpty(_logSearchText) ||
                 entry.Message.Contains(_logSearchText, StringComparison.OrdinalIgnoreCase) ||
                 entry.Level.Contains(_logSearchText, StringComparison.OrdinalIgnoreCase) ||
                 entry.Source.Contains(_logSearchText, StringComparison.OrdinalIgnoreCase)))
                _filteredServerLogs.Add(entry);
        }
        if (_autoScroll) AutoScrollRequested?.Invoke();
    }

    private void FilterServerLogs()
    {
        _filteredServerLogs.Clear();

        var searchLower = _logSearchText?.ToLower() ?? "";
        foreach (var e in _serverLogs)
        {
            if (!IsLevelVisible(e.Level)) continue;
            if (string.IsNullOrEmpty(searchLower) ||
                e.Message.ToLower().Contains(searchLower) ||
                e.Level.ToLower().Contains(searchLower) ||
                e.Source.ToLower().Contains(searchLower))
            {
                _filteredServerLogs.Add(e);
            }
        }
    }

    private bool IsLevelVisible(string level)
    {
        return level switch
        {
            "INFO" => ShowInfo,
            "DEBUG" => ShowDebug,
            "TRACE" => ShowTrace,
            "WARNING" => ShowWarning,
            "ERROR" => ShowError,
            "FATAL" => ShowFatal,
            _ => ShowInfo
        };
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
                if (_disposed || !IsConnected) return;
                await RefreshClientsAsync();
                await RefreshPendingAsync();
                await RefreshWhitelistsAsync();
            });
        }
        finally
        {
            Interlocked.Exchange(ref _refreshInProgress, 0);
        }
    }

    private string _certificateFingerprint = "";
    public string CertificateFingerprint
    {
        get => _certificateFingerprint;
        private set
        {
            if (SetProperty(ref _certificateFingerprint, value))
                CommandManager.InvalidateRequerySuggested();
        }
    }

    private async Task LoadCertificateFingerprintAsync()
    {
        var fingerprint = await _ipcService.GetCertificateFingerprintAsync();
        await Application.Current.Dispatcher.InvokeAsync(() =>
        {
            if (!_disposed) CertificateFingerprint = fingerprint;
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
            AddLog("INFO", $"Порт сервера сохранён: {port}. Перезапустите службу для применения порта и правил Windows Firewall");
        else
            AddLog("ERROR", "Ошибка сохранения порта сервера");
    }

    private async Task SaveAllServerSettingsAsync()
    {
        await SaveServerNameAsync();
        await SaveServerPortAsync();
        if (!await _ipcService.SetWindowsNotificationsAsync(WindowsNotificationsEnabled))
        {
            AddLog("ERROR", "Ошибка сохранения: уведомления Windows");
            return;
        }
        try
        {
            await _ipcService.TemporaryWhitelistCommandAsync("SET_SAVE_TEMPORARY_WHITELIST_STATE", SaveTemporaryWhitelistState ? "1" : "0");
        }
        catch (Exception ex) { AddLog("ERROR", $"Ошибка сохранения временных списков: {ex.Message}"); return; }
        HasSettingsChanges = false;
    }

    private async Task LoadTemporaryWhitelistSettingsAsync()
    {
        try
        {
            var data = await _ipcService.TemporaryWhitelistCommandAsync("GET_SAVE_TEMPORARY_WHITELIST_STATE");
            if (_disposed) return;
            _saveTemporaryWhitelistState = data.FirstOrDefault() == "1";
            OnPropertyChanged(nameof(SaveTemporaryWhitelistState));
        }
        catch (Exception ex) { if (!_disposed) AddLog("ERROR", $"Ошибка загрузки настройки временных списков: {ex.Message}"); }
    }

    private async Task LoadWindowsNotificationsAsync()
    {
        var enabled = await _ipcService.GetWindowsNotificationsAsync();
        await Application.Current.Dispatcher.InvokeAsync(() =>
        {
            if (!_disposed && enabled.HasValue)
            {
                _windowsNotificationsEnabled = enabled.Value;
                OnPropertyChanged(nameof(WindowsNotificationsEnabled));
            }
        });
    }

    private async Task RefreshServerStatusAsync()
    {
        if (_disposed || Interlocked.CompareExchange(ref _statusRefreshInProgress, 1, 0) != 0)
            return;
        try
        {
            var serverStatus = await _ipcService.GetServerStatusAsync();
            bool reconnected = false;
            await System.Windows.Application.Current.Dispatcher.InvokeAsync(() =>
            {
                if (_disposed) return;
                reconnected = !IsConnected && serverStatus.IsRunning;
                IsConnected = serverStatus.IsRunning;
                ServerStatus = serverStatus.IsRunning ? "Запущен" : "Не подключен";
                Uptime = serverStatus.IsRunning && !string.IsNullOrEmpty(serverStatus.UptimeText)
                    ? $"Аптайм: {serverStatus.UptimeText}" : "";
                ConnectedClientsCount = serverStatus.ConnectedClients;
                PendingCount = serverStatus.PendingCount;
                FiltrationStatus = serverStatus.IsRunning
                    ? (serverStatus.FiltrationEnabled ? "Вкл" : "Выкл")
                    : "N/A";
                if (!serverStatus.IsRunning)
                {
                    ConnectedClients.Clear();
                    PendingRegistrations.Clear();
                    RegisteredUsers.Clear();
                    RebuildUsersView();
                    Whitelists.Clear();
                    WhitelistConflicts = new();
                    OnPropertyChanged(nameof(ActiveWhitelistsText));
                    OnPropertyChanged(nameof(ActiveTemporaryWhitelistsText));
                    CertificateFingerprint = "";
                }
                LastUpdate = DateTime.Now;
            });
            if (reconnected && !_disposed)
            {
                _ = RefreshAllAsync();
                _ = LoadServerNameAsync();
                _ = LoadServerPortAsync();
                _ = LoadWindowsNotificationsAsync();
                _ = LoadTemporaryWhitelistSettingsAsync();
                _ = LoadCertificateFingerprintAsync();
                _ = LoadClientUpdateAsync();
                _ = LoadLogPathAsync();
            }
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
                    ConnectedClientsCount = 0;
                    PendingCount = 0;
                    FiltrationStatus = "N/A";
                    ConnectedClients.Clear();
                    PendingRegistrations.Clear();
                    RegisteredUsers.Clear();
                    RebuildUsersView();
                    Whitelists.Clear();
                    WhitelistConflicts = new();
                    OnPropertyChanged(nameof(ActiveWhitelistsText));
                    OnPropertyChanged(nameof(ActiveTemporaryWhitelistsText));
                    CertificateFingerprint = "";
                    AddLog("ERROR", $"Ошибка: {ex.Message}");
                });
            }
            catch { }
        }
        finally
        {
            Interlocked.Exchange(ref _statusRefreshInProgress, 0);
        }
    }

    private void RebuildUsersView()
    {
        var searchLower = UsersSearchText?.ToLower() ?? "";

        UserTableView.UpdateOverview(AllUsers, RegisteredUsers, ConnectedClients, PendingRegistrations);
        UserTableView.Update(FilteredAllUsers, AllUsers.Where(u =>
            searchLower == "" || u.Name.ToLower().Contains(searchLower) || u.Address.ToLower().Contains(searchLower)));
        UserTableView.Update(FilteredConnected, ConnectedClients.Where(c =>
            searchLower == "" || c.Name.ToLower().Contains(searchLower) || c.Address.ToLower().Contains(searchLower)));
        UserTableView.Update(FilteredPending, PendingRegistrations.Where(p =>
            searchLower == "" || p.Name.ToLower().Contains(searchLower) || p.Address.ToLower().Contains(searchLower)));
        NotifyUserSelectionChanged();
    }

    public async Task RefreshClientsAsync()
    {
        try
        {
            var registered = await _ipcService.GetRegisteredUsersAsync();
            var clients = await _ipcService.GetClientsAsync();
            await System.Windows.Application.Current.Dispatcher.InvokeAsync(() =>
            {
                var previous = RegisteredUsers.ToHashSet();
                UserTableView.UpdateRegistered(RegisteredUsers, ConnectedClients, registered, clients);
                foreach (var removed in previous.Except(RegisteredUsers)) removed.PropertyChanged -= OnUserSelectionChanged;
                foreach (var added in RegisteredUsers.Except(previous)) added.PropertyChanged += OnUserSelectionChanged;
                ConnectedClientsCount = ConnectedClients.Count;
                RebuildUsersView();
                CommandManager.InvalidateRequerySuggested();
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
                {
                    p.PropertyChanged -= OnUserSelectionChanged;
                    PendingRegistrations.Remove(p);
                }

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
                        np.PropertyChanged += OnUserSelectionChanged;
                        PendingRegistrations.Add(np);
                    }
                }
                PendingCount = PendingRegistrations.Count;
                RebuildUsersView();
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
        if (_disposed || !IsConnected || _savingWhitelistActivation) return;
        var revision = Volatile.Read(ref _whitelistActivationRevision);
        try
        {
            var whitelists = await _ipcService.GetWhitelistsAsync();
            if (_disposed || !IsConnected || _savingWhitelistActivation) return;
            var conflicts = await _ipcService.GetWhitelistConflictsAsync();
            await System.Windows.Application.Current.Dispatcher.InvokeAsync(() =>
            {
                if (_disposed || !IsConnected || _savingWhitelistActivation || revision != _whitelistActivationRevision) return;
                _whitelistActivation.Refresh(whitelists);
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
                        existing.IsActive = nw.IsActive;
                        existing.UpdateTimer(nw.Temporary == null ? null : new WhitelistTimerInfo
                        {
                            DurationSeconds = nw.Temporary.DurationSeconds,
                            RemainingSeconds = nw.RemainingSeconds,
                            AutoDelete = nw.Temporary.AutoDelete
                        }, nw.TimerIsRunning);
                    }
                    else
                    {
                        SubscribeWhitelistSelection(nw);
                        Whitelists.Add(nw);
                    }
                }
                WhitelistConflicts = conflicts;
                NotifyWhitelistActivationChanged();
                UpdateAllSelectedState();
            });
        }
        catch (Exception ex)
        {
            await System.Windows.Application.Current.Dispatcher.InvokeAsync(() =>
            {
                if (_disposed) return;
                if (!IsConnected)
                {
                    Whitelists.Clear();
                    WhitelistConflicts = new();
                    OnPropertyChanged(nameof(ActiveWhitelistsText));
                    OnPropertyChanged(nameof(ActiveTemporaryWhitelistsText));
                    CertificateFingerprint = "";
                    return;
                }
                AddLog("ERROR", $"Ошибка загрузки списков: {ex.Message}");
            });
        }
    }

    private List<ConnectedClient> GetClientTargets(string? name) =>
        UserTableView.SelectedOrCurrent(UsersShowAll ? RegisteredUsers : ConnectedClients,
            (UsersShowAll ? RegisteredUsers : ConnectedClients).FirstOrDefault(c => c.Name == name),
            UsersShowAll && AllUsers.Any(u => u.IsSelected));

    private List<ConnectedClient> GetConnectedTargets(string? name) =>
        GetClientTargets(name).Where(c => ConnectedClients.Contains(c)).ToList();

    private List<PendingRegistration> GetPendingTargets(string? name) =>
        UserTableView.SelectedOrCurrent(PendingRegistrations, PendingRegistrations.FirstOrDefault(p => p.Name == name),
            UsersShowAll && AllUsers.Any(u => u.IsSelected));

    private static void ClearUserSelection(IEnumerable<ISelectableRow> rows)
    {
        foreach (var row in rows) row.IsSelected = false;
    }

    private async Task RunUserActionAsync(Func<Task> action)
    {
        if (!CanRunUserAction) return;
        _userActionInProgress = true;
        CommandManager.InvalidateRequerySuggested();
        try { await action(); }
        catch (Exception ex) { AddLog("ERROR", $"Ошибка действия с пользователями: {ex.Message}"); }
        finally
        {
            _userActionInProgress = false;
            CommandManager.InvalidateRequerySuggested();
        }
    }

    private async Task ApproveUserAsync(string? name)
    {
        var targets = GetPendingTargets(name);
        ClearUserSelection(targets);
        foreach (var user in targets)
        {
            var ok = await _ipcService.ApproveUserAsync(user.Name);
            AddLog(ok ? "INFO" : "ERROR", ok
                ? $"Пользователь {user.Name} одобрен — токен будет доставлен клиенту автоматически"
                : $"Ошибка одобрения {user.Name}");
        }
        await RefreshPendingAsync();
        await RefreshClientsAsync();
    }

    private async Task ShowUserTokenAsync(string? name)
    {
        var targets = GetClientTargets(name);
        if (targets.Count == 0) return;
        ClearUserSelection(targets);
        var tokens = new List<string>();
        foreach (var user in targets)
        {
            var token = await _ipcService.GetUserTokenAsync(user.Name);
            tokens.Add($"Клиент «{user.Name}»:\n{(string.IsNullOrEmpty(token) ? "Токен не установлен или недоступен." : token)}");
        }
        MessageBox.Show(string.Join("\n\n", tokens), "Токены доступа", MessageBoxButton.OK, MessageBoxImage.Information);
    }

    private async Task RegenerateUserTokenAsync(string? name)
    {
        var targets = GetClientTargets(name);
        ClearUserSelection(targets);
        var updated = new List<string>();
        foreach (var user in targets)
        {
            var token = await _ipcService.RegenerateUserTokenAsync(user.Name);
            AddLog(string.IsNullOrEmpty(token) ? "ERROR" : "INFO",
                string.IsNullOrEmpty(token) ? $"Ошибка сброса токена {user.Name}" : $"Токен {user.Name} обновлён");
            if (!string.IsNullOrEmpty(token)) updated.Add(user.Name);
        }
        if (updated.Count > 0)
        {
            MessageBox.Show($"Токены обновлены: {string.Join(", ", updated)}. Клиенты получат новые токены автоматически после повторной проверки своих ключевых пар. Редактировать настройки клиентов не требуется.",
                "Токен доступа", MessageBoxButton.OK, MessageBoxImage.Information);
        }
    }

    private async Task RejectUserAsync(string? name)
    {
        var targets = GetPendingTargets(name);
        ClearUserSelection(targets);
        foreach (var user in targets)
        {
            var ok = await _ipcService.RejectUserAsync(user.Name);
            AddLog(ok ? "INFO" : "ERROR", ok ? $"Пользователь {user.Name} отклонен" : $"Ошибка отклонения {user.Name}");
        }
        await RefreshPendingAsync();
    }

    private async Task DisconnectUserAsync(string? name)
    {
        var targets = GetConnectedTargets(name);
        ClearUserSelection(targets);
        foreach (var user in targets)
        {
            var ok = await _ipcService.DisconnectUserAsync(user.Name);
            AddLog(ok ? "INFO" : "ERROR", ok ? $"Пользователь {user.Name} отключен" : $"Ошибка отключения {user.Name}");
        }
        await RefreshClientsAsync();
    }

    private bool CanSetClientIpv6Block(ConnectedClient? client)
    {
        if (!CanRunUserAction) return false;
        var targets = GetConnectedTargets(client?.Name);
        return targets.Count > 0 && targets.All(c => c.Policy?.CanSetIpv6Block == true);
    }

    private async Task SetClientIpv6BlockAsync(ConnectedClient? client, bool blockIpv6)
    {
        var targets = GetConnectedTargets(client?.Name);
        if (targets.Count == 0 || targets.Any(c => c.Policy?.CanSetIpv6Block != true)) return;
        var pending = targets.ToDictionary(c => c.Name, _ => blockIpv6);
        ClearUserSelection(targets);
        foreach (var (name, enabled) in pending.ToList())
        {
            try { await _ipcService.SetClientIpv6BlockAsync(name, enabled); }
            catch (Exception ex)
            {
                AddLog("ERROR", $"Клиент {name}: {ex.Message}");
                pending.Remove(name);
            }
        }
        var deadline = DateTime.UtcNow.AddSeconds(12);
        while (pending.Count > 0 && !_disposed && DateTime.UtcNow < deadline)
        {
            await RefreshClientsAsync();
            foreach (var (name, enabled) in pending.ToList())
            {
                var current = ConnectedClients.FirstOrDefault(c => c.Name == name);
                if (current == null)
                {
                    AddLog("ERROR", $"Клиент {name}: клиент отключился");
                    pending.Remove(name);
                }
                else if (current.Policy?.BlockIpv6 == enabled)
                {
                    AddLog("INFO", $"Клиент {name}: блокировка IPv6 {(enabled ? "включена" : "выключена")}");
                    pending.Remove(name);
                }
            }
            if (pending.Count > 0) await Task.Delay(500);
        }
        foreach (var name in pending.Keys)
            AddLog("ERROR", $"Клиент {name}: клиент не подтвердил изменение блокировки IPv6");
    }

    private async Task DeleteSelectedWhitelistsAsync(WhitelistInfo? whitelist)
    {
        var selected = CurrentWhitelists.Where(w => w.IsSelected).Select(w => w.Name).ToList();
        if (selected.Count == 0 && whitelist != null) selected.Add(whitelist.Name);
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

    private void NotifyWhitelistActivationChanged()
    {
        OnPropertyChanged(nameof(HasWhitelistActivationChanges));
        OnPropertyChanged(nameof(CanUseWhitelistPresets));
        OnPropertyChanged(nameof(ActiveWhitelistsText));
        OnPropertyChanged(nameof(ActiveTemporaryWhitelistsText));
        OnPropertyChanged(nameof(WhitelistConflictsText));
        CommandManager.InvalidateRequerySuggested();
    }

    private void SetWhitelistsActive(WhitelistInfo? whitelist, bool active)
    {
        if (!IsConnected || _savingWhitelistActivation) return;
        var selected = CurrentWhitelists.Where(w => w.IsSelected).ToList();
        if (selected.Count == 0 && whitelist != null) selected.Add(whitelist);
        foreach (var row in selected) _whitelistActivation.SetActive(row, active);
        IsAllSelected = false;
        NotifyWhitelistActivationChanged();
    }

    private void CancelWhitelistActivation()
    {
        if (_savingWhitelistActivation) return;
        _whitelistActivation.Cancel(Whitelists);
        NotifyWhitelistActivationChanged();
    }

    private async Task SyncWhitelistsAsync()
    {
        if (!IsConnected || _syncingWhitelists || _savingWhitelistActivation) return;
        _syncingWhitelists = true;
        CommandManager.InvalidateRequerySuggested();
        try
        {
            var ok = await Task.Run(() => _ipcService.SyncWhitelistsAsync());
            AddLog(ok ? "INFO" : "ERROR", ok
                ? "Запрошена синхронизация белых списков. Клиенты с отличающимися правилами загрузят сохранённый список сервера."
                : "Не удалось запросить синхронизацию белых списков.");
        }
        catch (Exception ex)
        {
            AddLog("ERROR", $"Ошибка синхронизации белых списков: {ex.Message}");
        }
        finally
        {
            _syncingWhitelists = false;
            CommandManager.InvalidateRequerySuggested();
        }
    }

    public Task<List<WhitelistPreset>> GetWhitelistPresetsAsync() => _ipcService.GetWhitelistPresetsAsync();
    public Task<List<string>> GetWhitelistPreviewEntriesAsync(string name) => _ipcService.GetWhitelistPreviewEntriesAsync(name);

    private async Task SaveWhitelistPresetAsync()
    {
        if (!CanUseWhitelistPresets) return;
        _openingPresetEditor = true;
        NotifyWhitelistActivationChanged();
        var selection = new WhitelistPreset
        {
            ActiveWhitelists = Whitelists.Where(w => w.IsActive).Select(w => w.Name)
                .OrderBy(name => name, StringComparer.CurrentCultureIgnoreCase).ToList()
        };
        try
        {
            var presets = await GetWhitelistPresetsAsync();
            var window = new WhitelistPresetWindow(presets, selection, async (name, replace) =>
            {
                await _ipcService.SaveWhitelistPresetAsync(name, replace, selection.ActiveWhitelists);
                AddLog("INFO", $"Пресет сохранён: {name}");
            }) { Owner = Application.Current.MainWindow };
            window.ShowDialog();
        }
        catch (Exception ex)
        {
            MessageBox.Show($"Не удалось открыть сохранение пресета.\n{ex.Message}", "Сохранить пресет", MessageBoxButton.OK, MessageBoxImage.Error);
        }
        finally
        {
            _openingPresetEditor = false;
            NotifyWhitelistActivationChanged();
        }
    }

    private async Task LoadWhitelistPresetAsync(WhitelistPreset preset)
    {
        if (!CanUseWhitelistPresets) return;
        _savingWhitelistActivation = true;
        NotifyWhitelistActivationChanged();
        try
        {
            await _ipcService.LoadWhitelistPresetAsync(preset.Name);
            _whitelistActivation.Cancel(Whitelists);
            Interlocked.Increment(ref _whitelistActivationRevision);
            AddLog("INFO", $"Пресет применён: {preset.Name}");
        }
        catch (Exception ex)
        {
            MessageBox.Show($"Не удалось загрузить пресет.\n{ex.Message}", "Загрузить пресет", MessageBoxButton.OK, MessageBoxImage.Error);
        }
        finally
        {
            _savingWhitelistActivation = false;
            NotifyWhitelistActivationChanged();
        }
        await RefreshWhitelistsAsync();
    }

    private async Task SaveWhitelistActivationAsync()
    {
        if (!IsConnected || _savingWhitelistActivation || !HasWhitelistActivationChanges) return;
        var changes = _whitelistActivation.GetChanges();
        _savingWhitelistActivation = true;
        NotifyWhitelistActivationChanged();
        try
        {
            var ok = await Task.Run(() => _ipcService.SetWhitelistsActiveAsync(changes));
            if (ok)
            {
                _whitelistActivation.Accept(changes);
                Interlocked.Increment(ref _whitelistActivationRevision);
                AddLog("INFO", $"Активность списков сохранена: {changes.Count}");
            }
            else
                AddLog("ERROR", "Не удалось сохранить активность списков. Изменения сохранены в черновике.");
        }
        catch (Exception ex)
        {
            AddLog("ERROR", $"Ошибка сохранения активности списков: {ex.Message}");
        }
        finally
        {
            _savingWhitelistActivation = false;
            NotifyWhitelistActivationChanged();
        }
        await RefreshWhitelistsAsync();
    }

    private async Task OpenDomainCheckAsync()
    {
        if (_openingDomainCheck) return;
        _openingDomainCheck = true;
        CommandManager.InvalidateRequerySuggested();
        try
        {
            var entries = await Task.Run(() => _ipcService.GetComposedWhitelistAsync());
            var window = new DomainCheckWindow(entries) { Owner = Application.Current.MainWindow };
            window.ShowDialog();
        }
        catch (Exception ex)
        {
            AddLog("ERROR", $"Ошибка проверки домена: {ex.Message}");
            MessageBox.Show($"Не удалось загрузить общий белый список.\n{ex.Message}",
                "Проверка домена", MessageBoxButton.OK, MessageBoxImage.Error);
        }
        finally
        {
            _openingDomainCheck = false;
            CommandManager.InvalidateRequerySuggested();
        }
    }

    private void OpenWhitelistTimer(WhitelistInfo? whitelist)
    {
        if (whitelist?.Temporary == null || _openingTimerEditor) return;
        _openingTimerEditor = true;
        try
        {
            var dialog = new WhitelistTimerWindow(whitelist, async timer =>
            {
                await _ipcService.TemporaryWhitelistCommandAsync("UPDATE_WHITELIST_TIMER", whitelist.Name,
                    timer.DurationSeconds.ToString(CultureInfo.InvariantCulture),
                    timer.RemainingSeconds?.ToString(CultureInfo.InvariantCulture) ?? "unchanged", timer.AutoDelete ? "1" : "0");
                await RefreshWhitelistsAsync();
            }) { Owner = Application.Current.MainWindow };
            dialog.ShowDialog();
        }
        finally { _openingTimerEditor = false; CommandManager.InvalidateRequerySuggested(); }
    }

    private async Task ResetWhitelistTimersAsync(WhitelistInfo? whitelist)
    {
        var selected = CurrentWhitelists.Where(w => w.IsSelected).ToList();
        if (selected.Count == 0 && whitelist != null) selected.Add(whitelist);
        if (selected.Count == 0) return;
        try
        {
            await _ipcService.TemporaryWhitelistCommandAsync("RESET_WHITELIST_TIMERS", selected.Select(w => w.Name).ToArray());
            await RefreshWhitelistsAsync();
        }
        catch (Exception ex) { AddLog("ERROR", $"Ошибка сброса таймера: {ex.Message}"); }
    }

    private Task OpenCreateWhitelistAsync() => ShowWhitelistEditorAsync();

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

        await ShowWhitelistEditorAsync(whitelist.Name, initialEntries);
    }

    private async Task ShowWhitelistEditorAsync(string? initialName = null, List<string>? initialEntries = null)
    {
        if (_openingWhitelistEditor) return;
        _openingWhitelistEditor = true;
        CommandManager.InvalidateRequerySuggested();
        var savedName = initialName;
        var temporary = initialName == null && ShowTemporaryWhitelists;
        try
        {
            await _whitelistDialog.ShowDialogAsync(
                initialName == null ? WhitelistDialogMode.Create : WhitelistDialogMode.Edit,
                async result =>
                {
                    var ok = true;
                    if (savedName == null)
                    {
                        if (temporary)
                        {
                            await _ipcService.TemporaryWhitelistCommandAsync("CREATE_TEMPORARY_WHITELIST", result.Name, "900", "0");
                        }
                        else ok = await _ipcService.CreateWhitelistAsync(result.Name);
                        if (ok) savedName = result.Name;
                    }
                    else if (savedName != result.Name)
                    {
                        ok = await _ipcService.RenameWhitelistAsync(savedName, result.Name);
                        if (ok)
                        {
                            _whitelistActivation.Rename(savedName, result.Name);
                            savedName = result.Name;
                        }
                    }

                    if (ok) ok = await _ipcService.SaveWhitelistAsync(result.Name, result.Entries);
                    AddLog(ok ? "INFO" : "ERROR", ok ? $"Список {result.Name} сохранен" : $"Ошибка сохранения списка {result.Name}");
                    await RefreshWhitelistsAsync();
                    return ok;
                }, initialName, initialEntries);
        }
        finally
        {
            _openingWhitelistEditor = false;
            CommandManager.InvalidateRequerySuggested();
        }
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

        var temporary = ShowTemporaryWhitelists;
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
                                await SaveWhitelistFromImport(currentName, currentEntries, existingNames, imported, errors, temporary);
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
                    await SaveWhitelistFromImport(currentName, currentEntries, existingNames, imported, errors, temporary);
                }
            }
            catch (Exception ex)
            {
                errors.Add($"{Path.GetFileName(filePath)}: {ex.Message}");
            }
        }

        await RefreshWhitelistsAsync();

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
                UpdateAllSelectedState();
                CommandManager.InvalidateRequerySuggested();
            }
        };
    }

    private void UpdateAllSelectedState()
    {
        _isAllSelected = CurrentWhitelists.Any() && CurrentWhitelists.All(w => w.IsSelected);
        OnPropertyChanged(nameof(IsAllSelected));
    }

    private async Task SaveWhitelistFromImport(string name, List<string> entries, HashSet<string> existingNames, List<string> imported, List<string> errors, bool temporary)
    {
        try
        {
            var analysis = WhitelistRules.Analyze(string.Join("\n", entries));
            if (analysis.HasErrors)
            {
                errors.Add($"{name}: {string.Join("\n", analysis.Issues)}");
                return;
            }
            entries = analysis.Rules.Select(r => r.Text).ToList();
            if (existingNames.Contains(name))
            {
                errors.Add($"{name}: список с таким именем уже существует");
                return;
            }
            var ok = true;
            if (temporary)
                await _ipcService.TemporaryWhitelistCommandAsync("CREATE_TEMPORARY_WHITELIST", name, "900", "0");
            else ok = await _ipcService.CreateWhitelistAsync(name);
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
        var selected = CurrentWhitelists.Where(w => w.IsSelected).ToList();
        if (selected.Count == 0)
        {
            selected = CurrentWhitelists.ToList();
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
        if (_disposed) return;
        _logUpdates.Add(new ServerLogEntry
        {
            Timestamp = DateTime.Now.ToString("HH:mm:ss"),
            Level = level,
            Source = "UI",
            Message = message
        });
    }

    private bool _disposed;

    public void Dispose()
    {
        if (_disposed) return;
        _disposed = true;
        _countdownTimer.Stop();
        foreach (var user in AllUsers) user.Dispose();
        _logUpdates.Dispose();
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
