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
    FiltrationOff,
    Critical
}

public enum NavigationSection
{
    Main,
    Whitelist,
    Updates,
    Settings
}

public class MainViewModel : ViewModelBase, IDisposable
{
    private const int MaxLogs = 500;

    public UpdatesViewModel Updates { get; } = new(ClientUpdates.InstalledVersion,
        async () => { using var ipc = new IpcService(); return await ClientUpdates.CheckAsync(ipc); },
        update => ((App)System.Windows.Application.Current).InstallUpdateAsync(update));

    // Services
    private readonly IpcService _ipcService = new IpcService();
    private readonly BrokerLogReader _clientLogReader = new("client", Lib.LogSource.Client);
    private readonly BrokerLogReader _firewallLogReader = new("firewall", Lib.LogSource.Firewall);

    /// <summary>Shared pipe service used by the ViewModel and the main window.</summary>
    public IpcService IpcService => _ipcService;

    private int _lastClientPid;

    // Status
    private bool _filtrationEnabled = false;
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

    private bool _firewallRunning;
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
        HealthState.AllOk => "Работает",
        HealthState.Degraded => "Отсутствует подключение к серверу",
        HealthState.FiltrationOff => "Фильтрация отключена",
        HealthState.Critical => BackendConnected ? "Firewall остановлен" : "Нет связи с клиентской службой",
        _ => "Проверка статуса..."
    };

    private NavigationSection _selectedSection = NavigationSection.Main;
    private bool _backendConnected;

    public bool BackendConnected
    {
        get => _backendConnected;
        private set
        {
            if (SetProperty(ref _backendConnected, value) && !value)
                SelectedSection = NavigationSection.Main;
        }
    }

    public NavigationSection SelectedSection
    {
        get => _selectedSection;
        set
        {
            if (value != NavigationSection.Main && !BackendConnected) return;
            if (SetProperty(ref _selectedSection, value) && value == NavigationSection.Settings)
            {
                _ = RefreshSavedFingerprintAsync();
                _ = RefreshIpv6BlockAsync();
            }
        }
    }

    public int WhitelistCount
    {
        get => _whitelistCount;
        set => SetProperty(ref _whitelistCount, value);
    }

    private bool _isWhitelistEmpty;
    public bool IsWhitelistEmpty
    {
        get => _isWhitelistEmpty;
        private set => SetProperty(ref _isWhitelistEmpty, value);
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

    private bool _filtrationAutoDisable = true;

    public bool FiltrationAutoDisable
    {
        get => _filtrationAutoDisable;
        set
        {
            if (SetProperty(ref _filtrationAutoDisable, value)) MarkSettingsChanged();
        }
    }

    private readonly Ipv6BlockPreference _ipv6BlockPreference = new();
    public bool Ipv6BlockEnabled
    {
        get => _ipv6BlockPreference.Value;
        set
        {
            if (_ipv6BlockPreference.Edit(value))
            {
                OnPropertyChanged();
                MarkSettingsChanged();
            }
        }
    }

    public bool HasSettingsChanges
    {
        get => _hasSettingsChanges;
        set => SetProperty(ref _hasSettingsChanges, value);
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
    private readonly object _refreshLock = new();
    private Task? _refreshTask;
    private bool _refreshQueued;

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
    public ICommand InspectCertificateCommand { get; }
    public ICommand ApplyCertificateFingerprintCommand { get; }
    public ICommand ToggleFiltrationCommand { get; }
    public ClientControlsViewModel Controls { get; }
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

    public Task RefreshStateAsync()
    {
        lock (_refreshLock)
        {
            _refreshQueued = true;
            if (_refreshTask != null) return _refreshTask;
            var completion = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
            _refreshTask = completion.Task;
            _ = RefreshStateLoopAsync(completion);
            return completion.Task;
        }
    }

    private async Task RefreshStateLoopAsync(TaskCompletionSource completion)
    {
        try
        {
            while (true)
            {
                lock (_refreshLock)
                {
                    if (!_refreshQueued || _disposed)
                    {
                        _refreshTask = null;
                        completion.SetResult();
                        return;
                    }
                    _refreshQueued = false;
                }
                await RefreshStateCoreAsync().ConfigureAwait(false);
            }
        }
        catch (Exception error)
        {
            lock (_refreshLock)
            {
                _refreshTask = null;
                completion.SetException(error);
            }
        }
    }

    private async Task RefreshStateCoreAsync()
    {
        var serviceState = await ClientBackendService.GetStateAsync().ConfigureAwait(false);
        try
        {
            var status = await _ipcService.GetStatusAsync().ConfigureAwait(false);
            if (_disposed) return;
            if (status.IsConnected) await RefreshIpv6BlockAsync().ConfigureAwait(false);

            await System.Windows.Application.Current.Dispatcher.InvokeAsync(() =>
            {
                if (_disposed) return;
                Controls.UpdateState(serviceState, status.IsConnected, status.DaemonStatus == "RUNNING");
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
                var whitelist = await _ipcService.GetWhitelistAsync().ConfigureAwait(false);
                if (_disposed) return;

                if (whitelist != null)
                {
                    _lastWhitelistRevision = status.WhitelistRevision;
                    System.Windows.Application.Current.Dispatcher.BeginInvoke(() =>
                    {
                        if (_disposed) return;
                        WhitelistEntries.Clear();
                        foreach (var domain in whitelist)
                        {
                            WhitelistEntries.Add(WhitelistEntry.Parse(domain));
                        }

                        IsWhitelistEmpty = whitelist.Count == 0;
                        UpdateWhitelistDisplay();
                        AddLog("INFO", $"Whitelist updated ({whitelist.Count} domains)");
                    });
                }
            }
        }
        catch
        {
            if (_disposed) return;
            await System.Windows.Application.Current.Dispatcher.InvokeAsync(() =>
            {
                if (_disposed) return;
                Controls.UpdateState(serviceState, false, false);
                FirewallRunning = false;
                BackendConnected = false;
                SettingsAvailable = false;
                ServerConnected = false;
                FiltrationEnabled = false;
                OverallHealth = HealthState.Critical;
                OnPropertyChanged(nameof(OverallHealthText));
                StatusRefreshed?.Invoke(new ClientStatus { ClientPid = 0 });
            });
            UpdateLogReaders(new ClientStatus { ClientPid = 0 });
        }
    }

    public MainViewModel()
    {
        _logUpdates = new DispatcherLogQueue<LogEntry>(MaxLogs,
            System.Windows.Application.Current.Dispatcher, FlushPendingLogs);

        Controls = new ClientControlsViewModel(ClientBackendService.ControlAsync,
            async action => {
                if (!await _ipcService.ControlFirewallAsync(action))
                    throw new IOException(string.IsNullOrEmpty(_ipcService.LastError)
                        ? "Не удалось выполнить команду." : _ipcService.LastError);
            }, RefreshStateAsync, AddLog);

        ClearLogsCommand = new RelayCommand(_ => ClearLogs());
        ExportLogsCommand = new RelayCommand(_ => ExportLogs());
        RegisterCommand = new RelayCommand(async _ => await RegisterAsync());
        ConnectCommand = new RelayCommand(async _ => await ConnectAsync(), _ => Controls.BackendAvailable && !ServerConnected);
        DisconnectCommand = new RelayCommand(async _ => await DisconnectAsync(), _ => Controls.BackendAvailable && ServerConnected);
        ChangeServerCommand = new RelayCommand(async _ => await ChangeServerAsync(), _ => DiscoveryEnabled);
        SaveSettingsCommand = new RelayCommand(async _ => await SaveAllSettingsAsync());
        InspectCertificateCommand = new RelayCommand(async _ => await InspectCertificateAsync(), _ => !CertificateBusy);
        ApplyCertificateFingerprintCommand = new RelayCommand(async _ => await ApplyCertificateFingerprintAsync(), _ => CanApplyCertificateFingerprint);
        ToggleFiltrationCommand = new RelayCommand(async _ => await ToggleFiltrationAsync(),
            _ => Controls.BackendAvailable && FirewallRunning);
        RefreshStatusCommand = new RelayCommand(async _ => await RefreshStateAsync());
        OpenHistoryCommand = new RelayCommand(async _ => await OpenHistoryAsync());
        OpenArchivedLogCommand = new RelayCommand(_ => OpenArchivedLogs());
        JumpToBottomCommand = new RelayCommand(_ => ScrollToBottomRequested?.Invoke());

        // Initialize whitelist status polling timer (safety fallback; events drive updates)
        _statusTimer.Elapsed += OnStatusTimerElapsed;
        _statusTimer.AutoReset = true;
        _statusTimer.Enabled = true;

        // Event-driven updates from the daemon pipe
        _ipcService.StateChanged += OnStateChanged;
        _ipcService.StartEventSubscription();

        AddLog("INFO", "Клиент запущен");

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
        var notifications = await _ipcService.GetWindowsNotificationsAsync();
        await RefreshSavedFingerprintAsync();
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
            if (notifications.HasValue)
            {
                _windowsNotificationsEnabled = notifications.Value;
                OnPropertyChanged(nameof(WindowsNotificationsEnabled));
            }
            SettingsAvailable = available;
        });
        await RefreshIpv6BlockAsync();
    }

    private async Task RefreshIpv6BlockAsync()
    {
        var revision = _ipv6BlockPreference.Revision;
        var value = await _ipcService.GetIpv6BlockAsync().ConfigureAwait(false);
        await System.Windows.Application.Current.Dispatcher.InvokeAsync(() =>
        {
            if (!_disposed && _ipv6BlockPreference.Refresh(value, revision))
                OnPropertyChanged(nameof(Ipv6BlockEnabled));
        });
    }

    private int _savedFingerprintRefreshInProgress;
    private async Task RefreshSavedFingerprintAsync()
    {
        if (Interlocked.CompareExchange(ref _savedFingerprintRefreshInProgress, 1, 0) != 0) return;
        try
        {
            var pin = await _ipcService.GetSavedServerFingerprintAsync();
            var error = _ipcService.SavedFingerprintError;
            await System.Windows.Application.Current.Dispatcher.InvokeAsync(() =>
            {
                if (_disposed) return;
                if (pin != null) PinnedCertificateFingerprint = pin;
                if (_certificateInspection != null || CertificateBusy) return;
                CertificateStatus = pin == null
                    ? (error.Contains("unknown command", StringComparison.OrdinalIgnoreCase)
                        ? "Клиентский демон не поддерживает чтение отпечатка. Обновите демон и перезапустите службу Firewall."
                        : $"Не удалось прочитать сохранённый отпечаток: {error}")
                    : pin.Length == 0
                        ? "В настройках клиентского демона нет сохранённого отпечатка."
                        : "Сохранённый отпечаток загружен. Проверка сервера ещё не выполнена.";
            });
        }
        catch (Exception ex)
        {
            AddLog("ERROR", $"Чтение сохранённого отпечатка: {ex.Message}");
        }
        finally { Interlocked.Exchange(ref _savedFingerprintRefreshInProgress, 0); }
    }

    private CertificateInspection? _certificateInspection;
    private string _certificateEndpoint = "";
    private string _pinnedCertificateFingerprint = "";
    private string _observedCertificateFingerprint = "";
    private string _verifiedCertificateFingerprint = "";
    private string _certificateStatus = "Проверьте сертификат сохранённого сервера.";
    private bool _certificateBusy;

    public string CertificateEndpoint { get => _certificateEndpoint; private set => SetProperty(ref _certificateEndpoint, value); }
    public string PinnedCertificateFingerprint { get => _pinnedCertificateFingerprint; private set => SetProperty(ref _pinnedCertificateFingerprint, value); }
    public string ObservedCertificateFingerprint { get => _observedCertificateFingerprint; private set => SetProperty(ref _observedCertificateFingerprint, value); }
    public string CertificateStatus { get => _certificateStatus; private set => SetProperty(ref _certificateStatus, value); }
    public bool CertificateBusy
    {
        get => _certificateBusy;
        private set { if (SetProperty(ref _certificateBusy, value)) CommandManager.InvalidateRequerySuggested(); }
    }
    public string VerifiedCertificateFingerprint
    {
        get => _verifiedCertificateFingerprint;
        set { if (SetProperty(ref _verifiedCertificateFingerprint, value)) CommandManager.InvalidateRequerySuggested(); }
    }
    private static string NormalizeFingerprint(string value)
    {
        var hex = string.Concat(value.Where(c => !char.IsWhiteSpace(c) && c != ':')).ToUpperInvariant();
        return hex.Length == 64 && hex.All(char.IsAsciiHexDigit) ? hex : "";
    }
    public bool CanApplyCertificateFingerprint => !CertificateBusy && _certificateInspection != null &&
        NormalizeFingerprint(VerifiedCertificateFingerprint).Length == 64 &&
        NormalizeFingerprint(VerifiedCertificateFingerprint) == _certificateInspection.Observed;

    private async Task InspectCertificateAsync()
    {
        CertificateBusy = true;
        _certificateInspection = null;
        CertificateEndpoint = "";
        ObservedCertificateFingerprint = "";
        CertificateStatus = "Проверка сертификата…";
        try
        {
            var inspection = await _ipcService.InspectServerCertificateAsync();
            if (inspection == null)
            {
                CertificateStatus = "Не удалось получить сертификат. Сохранённое доверие не изменено.";
                return;
            }
            _certificateInspection = inspection;
            CertificateEndpoint = $"{inspection.Address}:{inspection.Port}";
            PinnedCertificateFingerprint = inspection.Pinned;
            ObservedCertificateFingerprint = inspection.Observed;
            CertificateStatus = NormalizeFingerprint(inspection.Pinned) == inspection.Observed
                ? "Сертификат соответствует сохранённому отпечатку."
                : "Отпечаток отличается. Сверьте его с действующим отпечатком в настройках сервера.";
        }
        catch (Exception ex)
        {
            CertificateStatus = "Ошибка проверки. Сохранённое доверие не изменено.";
            AddLog("ERROR", $"Проверка сертификата: {ex.Message}");
        }
        finally { CertificateBusy = false; }
    }

    private async Task ApplyCertificateFingerprintAsync()
    {
        if (!CanApplyCertificateFingerprint || _certificateInspection == null) return;
        var inspection = _certificateInspection;
        var verified = NormalizeFingerprint(VerifiedCertificateFingerprint);
        CertificateBusy = true;
        try
        {
            if (await _ipcService.UpdateServerFingerprintAsync(inspection, verified))
            {
                PinnedCertificateFingerprint = verified;
                CertificateStatus = "Проверенный отпечаток сохранён. Клиент переподключится автоматически.";
                VerifiedCertificateFingerprint = "";
                AddLog("INFO", "Проверенный отпечаток сертификата сервера сохранён");
            }
            else
            {
                CertificateStatus = "Отпечаток не сохранён. Проверьте сервер и повторите проверку сертификата.";
            }
        }
        catch (Exception ex)
        {
            CertificateStatus = "Не удалось подтвердить сохранение. Повторите проверку сертификата.";
            AddLog("ERROR", $"Обновление отпечатка: {ex.Message}");
        }
        finally
        {
            _certificateInspection = null;
            CertificateBusy = false;
        }
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
        var ipv6Revision = _ipv6BlockPreference.Revision;
        var ipv6Saved = await _ipcService.SetIpv6BlockAsync(Ipv6BlockEnabled);
        if (!ipv6Saved)
        {
            AddLog("ERROR", "Ошибка сохранения: блокировка IPv6");
            return;
        }
        _ipv6BlockPreference.ConfirmSave(ipv6Revision);
        if (!await _ipcService.SetWindowsNotificationsAsync(WindowsNotificationsEnabled))
        {
            AddLog("ERROR", "Ошибка сохранения: уведомления Windows");
            return;
        }
        HasSettingsChanges = _ipv6BlockPreference.IsEdited;
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
            MessageDialog.Show("Укажите имя пользователя в поле «Пользователь» и нажмите «Сохранить».", "Регистрация",
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
                MessageDialog.Show("Запрос на регистрацию принят. Ожидайте подтверждения.",
                    "Регистрация", System.Windows.MessageBoxButton.OK, System.Windows.MessageBoxImage.Information);
            }
            else
            {
                MessageDialog.Show("Ошибка регистрации.",
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

    private readonly DispatcherLogQueue<LogEntry> _logUpdates;

    public void AddLog(string level, string message)
    {
        if (_disposed) return;
        _logUpdates.Add(new LogEntry
        {
            Timestamp = DateTime.Now.ToString("HH:mm:ss"),
            Level = level,
            Message = message
        });
    }

    private void FlushPendingLogs(List<LogEntry> entries)
    {
        if (_disposed) return;
        int removeCount = Math.Max(0, Logs.Count + entries.Count - MaxLogs);
        for (int i = 0; i < removeCount; i++)
        {
            var removed = Logs[0];
            Logs.RemoveAt(0);
            FilteredLogs.Remove(removed);
        }
        foreach (var entry in entries)
        {
            Logs.Add(entry);
            if (IsLevelVisible(entry.Level) &&
                (string.IsNullOrEmpty(LogSearchText) ||
                 entry.Message.Contains(LogSearchText, StringComparison.OrdinalIgnoreCase) ||
                 entry.Level.Contains(LogSearchText, StringComparison.OrdinalIgnoreCase)))
                FilteredLogs.Add(entry);
        }
        if (AutoScroll) ScrollToBottomRequested?.Invoke();
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
        BackendConnected = status.IsConnected;
        OnPropertyChanged(nameof(OverallHealthText));
        var daemonRunning = status.DaemonStatus == "RUNNING";
        var serverConnected = status.IsServerSessionActive;
        FirewallRunning = daemonRunning;
        ServerConnected = serverConnected;
        FiltrationEnabled = status.FiltrationEnabled;

        if (!daemonRunning)
            OverallHealth = HealthState.Critical;
        else if (!serverConnected)
            OverallHealth = HealthState.Degraded;
        else if (!status.FiltrationEnabled)
            OverallHealth = HealthState.FiltrationOff;
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

    private Task StartLogReadersAsync()
    {
        _clientLogReader.OnNewLine -= OnLogLineReceived;
        _clientLogReader.OnNewLine += OnLogLineReceived;
        _firewallLogReader.OnNewLine -= OnLogLineReceived;
        _firewallLogReader.OnNewLine += OnLogLineReceived;
        _clientLogReader.Start();
        _firewallLogReader.Start();
        return Task.CompletedTask;
    }

    private void OnLogLineReceived(string line)
    {
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

    public Task OpenHistoryAsync()
    {
        var sources = new List<LogFileSource>();
        foreach (var source in new[] { "firewall", "client" })
        {
            sources.Add(new LogFileSource {
                Label = source == "firewall" ? "Firewall" : "Client",
                LoadAsync = token => _ipcService.ReadLogHistoryAsync(source, token)
            });
        }
        LogViewRequested?.Invoke(sources);
        return Task.CompletedTask;
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
        _logUpdates.Dispose();
        _statusTimer?.Stop();
        _statusTimer?.Dispose();
        _ipcService.StateChanged -= OnStateChanged;
        _ipcService.StopEventSubscription();
        _clientLogReader.Stop();
        _firewallLogReader.Stop();
        _ipcService.Dispose();
    }
}
