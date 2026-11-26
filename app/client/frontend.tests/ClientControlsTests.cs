using frontend.Services;
using frontend.ViewModels;

internal static class ClientControlsTests
{
    private static void Check(bool value, string message)
    {
        if (!value) throw new Exception(message);
    }

    public static async Task RunAsync()
    {
        var calls = new List<string>();
        var errors = new List<string>();
        var state = ClientServiceState.Stopped;
        bool connected = false, firewall = false, blacklist = true;
        ClientControlsViewModel controls = null!;
        controls = new ClientControlsViewModel(action =>
        {
            calls.Add("backend:" + action);
            state = action == "stop" ? ClientServiceState.Stopped : ClientServiceState.Running;
            connected = state == ClientServiceState.Running;
            firewall = false;
            return Task.CompletedTask;
        }, action =>
        {
            calls.Add("firewall:" + action);
            firewall = action != "stop";
            return Task.CompletedTask;
        }, enabled =>
        {
            calls.Add("blacklist:" + enabled);
            blacklist = enabled;
            return Task.CompletedTask;
        }, () =>
        {
            controls.UpdateState(state, connected, firewall, blacklist);
            return Task.CompletedTask;
        }, (level, message) => { if (level == "ERROR") errors.Add(message); });

        foreach (var unavailable in new[] { ClientServiceState.Unknown, ClientServiceState.Missing,
            ClientServiceState.Unavailable, ClientServiceState.Starting, ClientServiceState.Stopping })
        {
            controls.UpdateState(unavailable, false, false);
            Check(!controls.StartBackendCommand.CanExecute(null) && !controls.StopBackendCommand.CanExecute(null)
                && !controls.RestartBackendCommand.CanExecute(null) && !controls.StartFirewallCommand.CanExecute(null),
                "Unavailable or transitioning service must not accept controls");
        }
        controls.UpdateState(state, connected, firewall);
        Check(calls.Count == 0, "Opening and refreshing the frontend must not start services");
        Check(controls.StartBackendCommand.CanExecute(null) && !controls.StartFirewallCommand.CanExecute(null),
            "Offline frontend must offer backend start only");
        await controls.ControlAsync(false, "start");
        Check(calls.Count == 0, "Firewall commands must be blocked while the backend is stopped");
        await controls.ControlAsync(true, "start");
        Check(calls.SequenceEqual(new[] { "backend:start" }) && !controls.FirewallRunning,
            "Starting the client must not implicitly start the firewall");
        Check(controls.StartFirewallCommand.CanExecute(null) && controls.StopBackendCommand.CanExecute(null)
            && !controls.StartBackendCommand.CanExecute(null), "Running backend must enable independent firewall start");
        Check(controls.ToggleProcessBlacklistCommand.CanExecute(null), "Local blacklist must be available with firewall stopped");
        controls.UpdateState(state, connected, firewall, true, true, true);
        Check(controls.ProcessBlacklistActive && controls.ProcessBlacklistStatusText == "Вкл",
            "The blacklist must show enabled when both switches are on");
        controls.UpdateState(state, connected, firewall, true, true, false);
        Check(!controls.ProcessBlacklistActive && controls.ProcessBlacklistStatusText == "Выкл на сервере"
            && controls.ToggleProcessBlacklistCommand.CanExecute(null) && controls.ProcessBlacklistActionText == "Выключить здесь",
            "A server disable must turn off the indicator and preserve the local toggle");
        controls.UpdateState(state, connected, firewall, true, false, true);
        Check(!controls.ProcessBlacklistActive && controls.ProcessBlacklistStatusText == "Нет связи с сервером",
            "A disconnect must clear stale server switch state");
        controls.UpdateState(state, connected, firewall, true, true, null);
        Check(!controls.ProcessBlacklistActive && controls.ProcessBlacklistStatusText == "Состояние на сервере неизвестно",
            "An unknown server switch must remain unavailable until synchronized");
        controls.UpdateState(state, connected, firewall, false, true, true);
        Check(!controls.ProcessBlacklistActive && controls.ProcessBlacklistStatusText == "Выкл на этом устройстве",
            "A local disable must override a server enable");
        controls.UpdateState(state, connected, firewall, true, true, false);
        await controls.ToggleProcessBlacklistAsync();
        Check(!blacklist && controls.ProcessBlacklistStatusText == "Выкл на этом устройстве",
            "The panel must refresh the local preference after disabling the blacklist");
        await controls.ToggleProcessBlacklistAsync();
        Check(blacklist, "A second toggle must restore the local preference");
        controls.UpdateState(state, connected, firewall, null);
        Check(!controls.ToggleProcessBlacklistCommand.CanExecute(null), "Older services cannot toggle the blacklist");
        await controls.ControlAsync(false, "start");
        Check(controls.FirewallRunning && controls.StopFirewallCommand.CanExecute(null)
            && controls.RestartFirewallCommand.CanExecute(null) && !controls.StartFirewallCommand.CanExecute(null),
            "Running firewall must offer stop and restart");
        await controls.ControlAsync(false, "restart");
        await controls.ControlAsync(false, "stop");
        Check(controls.BackendRunning && !controls.FirewallRunning, "Stopping firewall must preserve backend controls");
        await controls.ControlAsync(false, "start");
        await controls.ControlAsync(true, "restart");
        Check(!controls.FirewallRunning && controls.BackendRunning, "Backend restart must leave firewall stopped");
        controls.UpdateState(ClientServiceState.Running, false, true);
        Check(controls.StopBackendCommand.CanExecute(null) && controls.RestartBackendCommand.CanExecute(null)
            && !controls.StartFirewallCommand.CanExecute(null) && !controls.FirewallRunning,
            "Unresponsive backend must remain stoppable without stale firewall state");
        await controls.ControlAsync(true, "stop");
        Check(calls.SequenceEqual(new[] { "backend:start", "blacklist:False", "blacklist:True", "firewall:start",
            "firewall:restart", "firewall:stop", "firewall:start", "backend:restart", "backend:stop" }), "Commands reached the wrong service");
        Check(controls.StartBackendCommand.CanExecute(null) && errors.Count == 0, "Offline controls must recover after stop");

        var pending = new TaskCompletionSource();
        var refresh = new TaskCompletionSource();
        int attempts = 0;
        var busy = new ClientControlsViewModel(_ => { attempts++; return pending.Task; },
            _ => throw new Exception("Unexpected firewall call"), _ => throw new Exception("Unexpected blacklist call"),
            () => refresh.Task,
            (level, message) => { if (level == "ERROR") errors.Add(message); });
        busy.UpdateState(ClientServiceState.Running, true, true, true);
        var operation = busy.ControlAsync(true, "restart");
        Check(busy.Busy && !busy.BackendAvailable && !busy.StopBackendCommand.CanExecute(null)
            && !busy.RestartFirewallCommand.CanExecute(null) && !busy.ToggleProcessBlacklistCommand.CanExecute(null),
            "All dependent actions must disable during an operation");
        await busy.ControlAsync(true, "restart");
        Check(attempts == 1, "Double click must not submit a duplicate operation");
        pending.SetException(new IOException("Service denied access"));
        Check(busy.Busy && !operation.IsCompleted, "Controls must wait for refreshed status before enabling");
        refresh.SetException(new IOException("Status unavailable"));
        await operation;
        Check(!busy.Busy && busy.OperationText == "" && errors.Any(e => e.Contains("Service denied access"))
            && errors.Any(e => e.Contains("Status unavailable")), "Failures must be reported and busy state cleared");
        Console.WriteLine("Client controls passed: offline startup, independent service routing, unresponsive backend, busy and failure recovery.");
    }
}
