using System.IO;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Shapes;
using System.Windows.Threading;
using frontend;
using frontend.ViewModels;

internal static partial class Program
{
    private static void TestServerControlPanel(MainWindow window)
    {
        var panel = (Expander)window.FindName("ServerControlPanel");
        var main = (MainViewModel)window.DataContext;
        var buttons = VisualChildren<Button>(panel).ToList();
        var network = buttons.Single(button => button.Command == main.Filtration.ToggleCommand);
        var apps = buttons.Single(button => button.Command == main.BlacklistSwitch.ToggleCommand);
        Check(panel.IsExpanded && !network.IsEnabled && !apps.IsEnabled, "Disconnected switches must be unavailable");
        Check(buttons.Any(button => button.Command == main.StartServiceCommand) &&
            buttons.Any(button => button.Command == main.StopServiceCommand) &&
            buttons.Any(button => button.Command == main.RestartServiceCommand), "Service controls must remain accessible");
        var logHeader = VisualChildren<TextBlock>(window).Single(label => label.Text == "ЛОГИ");
        double panelBottom = panel.TranslatePoint(new Point(0, panel.ActualHeight), window).Y;
        Check(panelBottom <= logHeader.TranslatePoint(new Point(), window).Y &&
            panelBottom <= ((ListBox)window.FindName("ServerLogListBox")).TranslatePoint(new Point(), window).Y,
            "The control panel must be above the log toolbar and entries");

        var dots = VisualChildren<Ellipse>(panel).ToList();
        var running = window.FindResource("StatusRunning");
        var inactive = window.FindResource("StatusDisconnected");
        Check(dots.Count == 3 && dots.All(dot => Equals(dot.Fill, inactive)),
            "Every block header must show an inactive dot while its state is unavailable");

        main.ServiceStatus = "Running";
        FlushLayout(window);
        var serviceDot = dots.Single(dot => dot.DataContext is true);
        Check(Equals(serviceDot.Fill, running), "The service dot must follow the running service even without a connection");
        main.ServiceStatus = "Stopped";
        FlushLayout(window);
        Check(Equals(serviceDot.Fill, inactive), "Stopping the service must turn off its dot");

        var appsHost = VisualChildren<ContentControl>(panel).Single(host => ReferenceEquals(host.Content, main.BlacklistSwitch));
        bool enabled = true;
        var policy = new ServerPolicySwitch(main.BlacklistSwitch.Name, () => Task.FromResult(enabled),
            _ => Task.CompletedTask, (_, _) => { });
        appsHost.SetCurrentValue(ContentControl.ContentProperty, policy);
        policy.RefreshAsync().GetAwaiter().GetResult();
        FlushLayout(window);
        var policyDot = VisualChildren<Ellipse>(appsHost).Single();
        Check(Equals(policyDot.Fill, running), "An enabled policy must light its header dot");
        enabled = false;
        policy.RefreshAsync().GetAwaiter().GetResult();
        FlushLayout(window);
        Check(Equals(policyDot.Fill, inactive), "Disabling the policy must turn off its dot");
        appsHost.SetCurrentValue(ContentControl.ContentProperty, main.BlacklistSwitch);
        FlushLayout(window);
        apps = VisualChildren<Button>(panel).Single(button => button.Command == main.BlacklistSwitch.ToggleCommand);

        panel.IsExpanded = false;
        FlushLayout(window);
        Check(!apps.IsVisible && !network.IsVisible, "Collapsing the panel must hide its controls");
        panel.IsExpanded = true;
        FlushLayout(window);
        Check(apps.IsVisible && network.IsVisible, "Expanding the panel must show both switches");

        double width = window.Width, height = window.Height;
        int fontSize = AppearanceSettings.Current.InterfaceFontSize;
        try
        {
            window.Width = window.MinWidth;
            window.Height = window.MinHeight;
            AppearanceSettings.Current.InterfaceFontSize = 28;
            FlushLayout(window);
            CheckControlsFit(panel, "server control panel at minimum window size and maximum interface font");
            var bounds = panel.TransformToAncestor(window).TransformBounds(new Rect(panel.RenderSize));
            Check(bounds.Bottom <= window.ActualHeight + 1, "The expanded control panel must fit in the main window");
        }
        finally
        {
            AppearanceSettings.Current.InterfaceFontSize = fontSize;
            window.Width = width;
            window.Height = height;
            FlushLayout(window);
        }
    }

    private static void TestServerPolicySwitch()
    {
        bool serverValue = true;
        bool reject = false;
        var messages = new List<string>();
        TaskCompletionSource<bool>? pending = null;
        var control = new ServerPolicySwitch("Чёрный список приложений",
            () => pending?.Task ?? Task.FromResult(serverValue),
            value =>
            {
                if (reject) throw new IOException("Read only");
                serverValue = value;
                return Task.CompletedTask;
            }, (level, _) => messages.Add(level));

        control.RefreshAsync().GetAwaiter().GetResult();
        Check(control.Status == "Вкл" && control.ToggleCommand.CanExecute(null), "Saved policy must be available");
        var staleRead = new TaskCompletionSource<bool>();
        pending = staleRead;
        var refresh = control.RefreshAsync();
        pending = null;
        control.ToggleCommand.Execute(null);
        staleRead.SetResult(true);
        DispatcherFlush();
        Check(refresh.IsCompletedSuccessfully && !serverValue && control.Status == "Выкл" && messages.Last() == "INFO",
            "An older read must not undo a successful toggle");

        reject = true;
        control.ToggleCommand.Execute(null);
        Check(control.Status == "Выкл" && messages.Last() == "ERROR", "Failed writes must keep the server state");
        serverValue = true;
        control.RefreshAsync().GetAwaiter().GetResult();
        Check(control.Status == "Вкл", "A remote change must update the displayed state");

        pending = new TaskCompletionSource<bool>();
        refresh = control.RefreshAsync();
        control.Disconnect();
        pending.SetResult(true);
        DispatcherFlush();
        Check(refresh.IsCompletedSuccessfully && !control.ToggleCommand.CanExecute(null) && control.Status == "Недоступно",
            "Disconnect must invalidate outstanding replies");
    }

    private static void DispatcherFlush() =>
        Dispatcher.CurrentDispatcher.Invoke(() => { }, DispatcherPriority.ApplicationIdle);
}
