using System.Windows.Input;
using frontend.Services;

namespace frontend.ViewModels;

public sealed class ClientControlsViewModel : ViewModelBase
{
    private readonly Func<string, Task> _backend;
    private readonly Func<string, Task> _firewall;
    private readonly Func<bool, Task> _setProcessBlacklist;
    private readonly Func<Task> _refresh;
    private readonly Action<string, string> _log;
    private ClientServiceState _state;
    private bool _connected, _firewallRunning;
    private bool _serverConnected;
    private bool? _processBlacklistEnabled;
    private bool? _serverProcessBlacklistEnabled;

    public bool Busy { get; private set; }
    public bool BackendRunning => _state == ClientServiceState.Running;
    public bool BackendAvailable => !Busy && BackendRunning && _connected;
    public bool FirewallRunning => _firewallRunning;
    public string BackendStatusText => _state switch {
        ClientServiceState.Missing => "Не установлена",
        ClientServiceState.Unavailable => "Статус недоступен",
        ClientServiceState.Stopped => "Остановлена",
        ClientServiceState.Starting => "Запускается…",
        ClientServiceState.Stopping => "Останавливается…",
        ClientServiceState.Running => _connected ? "Работает" : "Запущена, нет связи",
        _ => "Проверка статуса…"
    };
    public string FirewallStatusText => !_connected ? "Нет связи с клиентской службой"
        : _firewallRunning ? "Запущен" : "Остановлен";
    public bool ProcessBlacklistActive => _processBlacklistEnabled == true && _serverProcessBlacklistEnabled == true;

    public string ProcessBlacklistStatusText => _processBlacklistEnabled switch
    {
        null => _connected ? "Недоступно" : "Нет связи с клиентской службой",
        false => "Выкл на этом устройстве",
        true when !_serverConnected => "Нет связи с сервером",
        true => _serverProcessBlacklistEnabled switch
        {
            true => "Вкл",
            false => "Выкл на сервере",
            null => "Состояние на сервере неизвестно"
        }
    };

    public string ProcessBlacklistActionText => _processBlacklistEnabled switch
    {
        true => "Выключить здесь",
        false => "Включить здесь",
        null => "Вкл/Выкл"
    };

    public string OperationText { get; private set; } = "";
    public ICommand StartBackendCommand { get; }
    public ICommand StopBackendCommand { get; }
    public ICommand RestartBackendCommand { get; }
    public ICommand StartFirewallCommand { get; }
    public ICommand StopFirewallCommand { get; }
    public ICommand RestartFirewallCommand { get; }
    public ICommand ToggleProcessBlacklistCommand { get; }

    public ClientControlsViewModel(Func<string, Task> backend, Func<string, Task> firewall,
        Func<bool, Task> setProcessBlacklist, Func<Task> refresh, Action<string, string> log)
    {
        _backend = backend;
        _firewall = firewall;
        _setProcessBlacklist = setProcessBlacklist;
        _refresh = refresh;
        _log = log;
        ICommand Command(bool client, string action) => new RelayCommand(
            async _ => await ControlAsync(client, action), _ => CanControl(client, action));
        StartBackendCommand = Command(true, "start");
        StopBackendCommand = Command(true, "stop");
        RestartBackendCommand = Command(true, "restart");
        StartFirewallCommand = Command(false, "start");
        StopFirewallCommand = Command(false, "stop");
        RestartFirewallCommand = Command(false, "restart");
        ToggleProcessBlacklistCommand = new RelayCommand(async _ => await ToggleProcessBlacklistAsync(),
            _ => !Busy && _processBlacklistEnabled.HasValue);
    }

    public void UpdateState(ClientServiceState state, bool connected, bool firewallRunning,
        bool? processBlacklistEnabled = null, bool serverConnected = false, bool? serverProcessBlacklistEnabled = null)
    {
        _state = state;
        _connected = connected;
        _firewallRunning = connected && firewallRunning;
        _serverConnected = connected && serverConnected;
        _processBlacklistEnabled = connected ? processBlacklistEnabled : null;
        _serverProcessBlacklistEnabled = _serverConnected ? serverProcessBlacklistEnabled : null;
        Changed();
    }

    private bool CanControl(bool backend, string action)
    {
        if (Busy) return false;
        if (backend) return action == "start" ? _state == ClientServiceState.Stopped
            : (action == "stop" || action == "restart") && BackendRunning;
        return BackendAvailable && (action == "start" ? !_firewallRunning
            : (action == "stop" || action == "restart") && _firewallRunning);
    }

    public Task ControlAsync(bool backend, string action)
    {
        if (!CanControl(backend, action)) return Task.CompletedTask;

        string target = backend ? "Клиентская служба" : "Firewall";
        string operation = action switch { "start" => "запуск", "stop" => "остановка", _ => "перезапуск" };
        return RunAsync(target, operation, () => backend ? _backend(action) : _firewall(action));
    }

    public Task ToggleProcessBlacklistAsync()
    {
        if (Busy || _processBlacklistEnabled is not bool enabled) return Task.CompletedTask;

        return RunAsync("Чёрный список приложений", enabled ? "выключение" : "включение",
            () => _setProcessBlacklist(!enabled));
    }

    private async Task RunAsync(string target, string operation, Func<Task> apply)
    {
        Busy = true;
        OperationText = $"{target}: {operation}…";
        Changed();
        _log("INFO", OperationText);
        try
        {
            await apply();
            _log("INFO", $"{target}: команда выполнена");
        }
        catch (Exception error) { _log("ERROR", $"{target}: {error.Message}"); }
        finally
        {
            try { await _refresh(); }
            catch (Exception error) { _log("ERROR", $"Не удалось обновить статус: {error.Message}"); }
            Busy = false;
            OperationText = "";
            Changed();
        }
    }

    private void Changed()
    {
        OnPropertyChanged("");
        CommandManager.InvalidateRequerySuggested();
    }
}
