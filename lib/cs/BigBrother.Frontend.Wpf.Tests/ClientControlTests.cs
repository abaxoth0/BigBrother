using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
using System.Windows.Media;
using frontend;
using frontend.Services;
using frontend.ViewModels;
using frontend.Views;

internal static partial class Program
{
    public sealed class ControlPanelData
    {
        public required ClientControlsViewModel Controls { get; init; }
        public bool ServerConnected => false;
        public bool FiltrationEnabled => false;
        public ICommand ConnectCommand { get; } = new RelayCommand(_ => { });
        public ICommand DisconnectCommand { get; } = new RelayCommand(_ => { });
        public ICommand ToggleFiltrationCommand { get; } = new RelayCommand(_ => { });
    }

    private static void TestClientControls()
    {
        Check(ClientBackendService.ServiceName == "BigBrother Client", "Backend controls must use the installed service name");
        var calls = new List<string>();
        var state = ClientServiceState.Stopped;
        bool firewall = false;
        ClientControlsViewModel controls = null!;
        controls = new ClientControlsViewModel(action =>
        {
            calls.Add("backend:" + action);
            state = action == "stop" ? ClientServiceState.Stopped : ClientServiceState.Running;
            firewall = false;
            return Task.CompletedTask;
        }, action =>
        {
            calls.Add("firewall:" + action);
            firewall = action != "stop";
            return Task.CompletedTask;
        }, () =>
        {
            controls.UpdateState(state, state == ClientServiceState.Running, firewall);
            return Task.CompletedTask;
        }, (_, _) => { });
        controls.UpdateState(state, false, false);
        var panel = new ClientControlPanel { DataContext = new ControlPanelData { Controls = controls } };
        var window = new Window { Content = panel, Width = 620, SizeToContent = SizeToContent.Height };
        Button Button(string name) => (Button)panel.FindName(name);
        try
        {
            window.Show();
            window.UpdateLayout();
            Check(calls.Count == 0 && Button("StartBackend").IsEnabled && !Button("StartFirewall").IsEnabled,
                "Opening the offline panel must offer backend start without starting anything");
            Check(((TextBlock)panel.FindName("BackendStatus")).Text == "Остановлена", "Offline status must be visible");
            foreach (int size in new[] { 10, 14, 28 })
            {
                AppearanceSettings.Current.InterfaceFontSize = size;
                window.UpdateLayout();
                foreach (string name in new[] { "StartBackend", "StopBackend", "RestartBackend", "StartFirewall", "StopFirewall", "RestartFirewall" })
                {
                    var button = Button(name);
                    var bounds = button.TransformToAncestor(panel).TransformBounds(new Rect(button.RenderSize));
                    Check(bounds.Left >= 0 && bounds.Right <= panel.ActualWidth && bounds.Bottom <= panel.ActualHeight,
                        "Service buttons must remain visible at all supported interface sizes");
                }
            }
            foreach (string name in new[] { "StartBackend", "StartFirewall", "RestartFirewall", "StopFirewall", "RestartBackend", "StopBackend" })
            {
                PumpUntil(() => Button(name).IsEnabled, name + " did not become enabled", busyRendering: false);
                Button(name).Command.Execute(null);
            }
            Check(calls.SequenceEqual(new[] { "backend:start", "firewall:start", "firewall:restart", "firewall:stop", "backend:restart", "backend:stop" }),
                "Rendered buttons must route to their own service");
            PumpUntil(() => Button("StartBackend").IsEnabled && !Button("StartFirewall").IsEnabled,
                "Stopping backend must restore offline controls", busyRendering: false);
            Check(window.IsVisible, "Stopping backend must leave the frontend open");
            Console.WriteLine("PASS: client control panel offline/start/stop/restart bindings and large-font layout");
        }
        finally
        {
            AppearanceSettings.Current.InterfaceFontSize = 14;
            window.Close();
        }
        TestOfflineMainWindow();
    }

    private static void TestOfflineMainWindow()
    {
        var state = ClientBackendService.GetStateAsync();
        PumpUntil(() => state.IsCompleted, "Service status query did not finish", busyRendering: false);
        if (state.Result != ClientServiceState.Missing && state.Result != ClientServiceState.Stopped)
            return;
        var window = new MainWindow { WindowState = WindowState.Normal, Width = 950, Height = 910 };
        try
        {
            if (!Fonts.SystemFontFamilies.Any(font => font.Source == "Consolas"))
                foreach (var control in SettingsLogicalElements<Control>(window))
                    if (control.FontFamily.Source == "Consolas") control.FontFamily = new FontFamily("Liberation Mono");
            window.Show();
            var vm = (MainViewModel)window.DataContext;
            var refresh = vm.RefreshStateAsync();
            Check(Enumerable.Range(0, 20).All(_ => ReferenceEquals(refresh, vm.RefreshStateAsync())),
                "Concurrent refresh callers must await the same complete status update");
            PumpUntil(() => refresh.IsCompleted, "Offline main window refresh stalled", busyRendering: false);
            refresh.GetAwaiter().GetResult();
            Check(window.IsVisible && !vm.BackendConnected && !vm.FirewallRunning &&
                vm.OverallHealthText == "Нет связи с клиентской службой", "Offline main window must remain usable and show backend status");
            Check(vm.Controls.StartBackendCommand.CanExecute(null) == (state.Result == ClientServiceState.Stopped),
                "Main window must offer start for an installed stopped backend only");
            Check(!vm.ConnectCommand.CanExecute(null) && !vm.ToggleFiltrationCommand.CanExecute(null),
                "Offline main window must disable backend-dependent commands");
            var after = ClientBackendService.GetStateAsync();
            PumpUntil(() => after.IsCompleted, "Service recheck did not finish", busyRendering: false);
            Check(after.Result == state.Result, "Opening and refreshing the main window must not start services");
            Console.WriteLine("PASS: complete offline main window, SCM status, coalesced refresh and no automatic service start");
        }
        finally { window.Close(); }
    }
}
