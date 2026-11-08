using System.IO;
using System.Runtime.InteropServices;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using System.Windows.Threading;
using frontend;
using frontend.Models;
using frontend.Services;
using frontend.ViewModels;
using frontend.Views;

internal static partial class Program
{
    [DllImport("user32.dll")]
    private static extern bool SetCursorPos(int x, int y);
    [DllImport("user32.dll")]
    private static extern void mouse_event(uint flags, uint dx, uint dy, uint data, UIntPtr extra);
    [DllImport("user32.dll")]
    private static extern void keybd_event(byte key, byte scan, uint flags, UIntPtr extra);

    private static void TestNetworkScheme()
    {
        string directory = System.IO.Path.Combine(System.IO.Path.GetTempPath(), "bb-map-wpf-" + Guid.NewGuid().ToString("N"));
        var store = new NetworkLayoutStore(System.IO.Path.Combine(directory, "layout.json"));
        var model = new NetworkSchemeViewModel(store);
        model.SetServer("Сервер офиса", "192.168.1.10");
        model.SetAvailable(true);
        using var online = new UserOverview(new ConnectedClient { Name = "Бухгалтерия", Address = "192.168.1.21",
            Policy = new ClientPolicyStatus { FiltrationEnabled = true } });
        using var offline = new UserOverview(new ConnectedClient { Name = "Переговорная", Address = "192.168.1.22" });
        using var pending = new UserOverview(new PendingRegistration { Name = "Новый компьютер", Address = "192.168.1.23" });
        online.SetOnline(true);
        model.Refresh([online, offline, pending], true);
        var view = new NetworkSchemeView { DataContext = model, Margin = new Thickness(10) };
        var window = new Window { Content = view, Width = 1180, Height = 800 };
        window.SetResourceReference(Control.ForegroundProperty, "TitleBarText");
        window.SetResourceReference(Control.BackgroundProperty, "WindowBackground");
        int textSize = AppearanceSettings.Current.TextFontSize;
        int interfaceSize = AppearanceSettings.Current.InterfaceFontSize;
        try
        {
            AppearanceSettings.Current.TextFontSize = 14;
            AppearanceSettings.Current.InterfaceFontSize = 14;
            window.Show();
            window.Activate();
            FlushLayout(window);
            var items = (ItemsControl)view.FindName("NodeItems");
            var client = model.Clients[0];
            Border Node(NetworkNode node) => VisualChildren<Border>(items).First(b => ReferenceEquals(b.DataContext, node) && b.Focusable);
            var border = Node(client);
            var status = VisualChildren<TextBlock>(border).Single(t => t.Text == "Подключён");
            Check(Equals(status.Foreground, Application.Current.FindResource("StatusRunning")),
                "Connected clients must have green status labels");
            var offlineStatus = VisualChildren<TextBlock>(Node(model.Clients.Single(n => n.Name == "Переговорная")))
                .Single(t => t.Text == "Не подключён");
            Check(Equals(offlineStatus.Foreground, Application.Current.FindResource("StatusDisconnected")),
                "Disconnected clients must have red status labels");
            Check(border.ActualWidth == 220 && border.ActualHeight == 120, "Client nodes must fit filtration in compact dimensions");
            var filtration = VisualChildren<TextBlock>(border).Single(t => t.Text == "Фильтрация: Вкл");
            Check(filtration.IsVisible && VisualChildren<TextBlock>(Node(model.Server)).All(t => !t.IsVisible || !t.Text.StartsWith("Фильтрация:")),
                "Filtration must be shown on client cards only");
            online.Client!.Policy = new ClientPolicyStatus { FiltrationEnabled = false };
            model.Refresh([online, offline, pending], true);
            FlushLayout(window);
            Check(filtration.Text == "Фильтрация: Выкл", "Filtration labels must update in place");
            online.Client.Policy = new ClientPolicyStatus { FiltrationEnabled = true };
            model.Refresh([online, offline, pending], true);
            Check(Equals(border.BorderBrush, Application.Current.FindResource("StatusRunning")) &&
                Equals(Node(model.Clients.Single(n => n.Name == "Переговорная")).BorderBrush, Application.Current.FindResource("StatusDisconnected")),
                "Client borders must show connected/disconnected state");
            border.Focus();
            double previousX = client.X;
            border.RaiseEvent(new KeyEventArgs(Keyboard.PrimaryDevice, PresentationSource.FromVisual(border), 0, Key.Right)
                { RoutedEvent = Keyboard.KeyDownEvent });
            FlushLayout(window);
            Check(client.X == previousX + 10 && store.Load()[client.Key].X == client.X,
                "Keyboard movement must move the node and save its position");

            var zoomLevel = (TextBlock)view.FindName("ZoomLevel");
            Check(!VisualChildren<Slider>(view).Any() && zoomLevel.Text == view.Zoom.ToString("P0"),
                "Zoom must be shown as a percentage without a slider");
            var scroll = (ScrollViewer)view.FindName("MapScroll");
            var canvas = (Grid)view.FindName("MapCanvas");
            var frame = (Border)view.FindName("MapFrame");
            var anchor = new Point(scroll.ViewportWidth * 0.75, scroll.ViewportHeight * 0.75);
            var world = scroll.TranslatePoint(anchor, canvas);
            var wheelPosition = scroll.PointToScreen(anchor);
            SetCursorPos((int)wheelPosition.X, (int)wheelPosition.Y);
            PumpNetworkInput();
            keybd_event(0x11, 0, 0, UIntPtr.Zero);
            PumpNetworkInput();
            mouse_event(0x0800, 0, 0, 120, UIntPtr.Zero);
            PumpNetworkInput();
            Check(Math.Abs(view.Zoom - 1.1) < 0.001 && zoomLevel.Text == view.Zoom.ToString("P0"),
                "Ctrl + wheel up must zoom in and update the displayed percentage");
            var afterZoom = scroll.TranslatePoint(anchor, canvas);
            Check((afterZoom - world).Length < 3, "Wheel zoom must preserve the world point under the pointer");
            mouse_event(0x0800, 0, 0, unchecked((uint)-120), UIntPtr.Zero);
            PumpNetworkInput();
            Check(view.Zoom == 1, "Ctrl + wheel down must zoom out");
            keybd_event(0x11, 0, 2, UIntPtr.Zero);
            PumpNetworkInput();
            mouse_event(0x0800, 0, 0, unchecked((uint)-120), UIntPtr.Zero);
            PumpNetworkInput();
            Check(view.Zoom == 1, "Wheel scrolling without Ctrl must not zoom");
            keybd_event(0x10, 0, 0, UIntPtr.Zero);
            PumpNetworkInput();
            mouse_event(0x0800, 0, 0, 120, UIntPtr.Zero);
            PumpNetworkInput();
            keybd_event(0x10, 0, 2, UIntPtr.Zero);
            Check(view.Zoom == 1, "Shift + wheel must no longer change zoom");

            foreach (double scale in new[] { 1d, 0.6 })
            {
                view.Zoom = scale;
                scroll.ScrollToHorizontalOffset(frame.Padding.Left);
                scroll.ScrollToVerticalOffset(frame.Padding.Top);
                FlushLayout(window);
                var start = border.PointToScreen(new Point(25, 25));
                double x = client.X, y = client.Y;
                SetCursorPos((int)start.X, (int)start.Y);
                PumpNetworkInput();
                mouse_event(0x0002, 0, 0, 0, UIntPtr.Zero);
                PumpNetworkInput();
                Check(border.IsMouseCaptured, "Mouse press must capture the draggable node");
                SetCursorPos((int)start.X + 60, (int)start.Y + 36);
                PumpNetworkInput();
                mouse_event(0x0004, 0, 0, 0, UIntPtr.Zero);
                PumpNetworkInput();
                Check(!border.IsMouseCaptured && Math.Abs(client.X - x - 60 / scale) < 3 && Math.Abs(client.Y - y - 36 / scale) < 3,
                    "Mouse dragging must follow the pointer at the current zoom");
                Check(Math.Abs(store.Load()[client.Key].X - client.X) < 0.1, "Mouse release must save the dragged position");

                var positions = model.Nodes.Select(n => (n.X, n.Y)).ToList();
                var panStart = scroll.PointToScreen(new Point(scroll.ViewportWidth - 100, scroll.ViewportHeight - 80));
                double horizontal = scroll.HorizontalOffset, vertical = scroll.VerticalOffset;
                SetCursorPos((int)panStart.X, (int)panStart.Y);
                PumpNetworkInput();
                mouse_event(0x0002, 0, 0, 0, UIntPtr.Zero);
                PumpNetworkInput();
                Check(frame.IsMouseCaptured && !border.IsMouseCaptured, "Dragging the background must capture the map");
                SetCursorPos((int)panStart.X - 80, (int)panStart.Y - 50);
                PumpNetworkInput();
                mouse_event(0x0004, 0, 0, 0, UIntPtr.Zero);
                PumpNetworkInput();
                Check(!frame.IsMouseCaptured && Math.Abs(scroll.HorizontalOffset - horizontal - 80) < 3 &&
                    Math.Abs(scroll.VerticalOffset - vertical - 50) < 3 && positions.SequenceEqual(model.Nodes.Select(n => (n.X, n.Y))),
                    "Background dragging must pan in screen pixels at any zoom without moving nodes");
            }

            view.Zoom = 1;
            model.Move(client, 20, 20);
            scroll.ScrollToHorizontalOffset(frame.Padding.Left - 160);
            scroll.ScrollToVerticalOffset(frame.Padding.Top - 160);
            FlushLayout(window);
            var signedStart = border.PointToScreen(new Point(25, 25));
            SetCursorPos((int)signedStart.X, (int)signedStart.Y);
            PumpNetworkInput();
            mouse_event(0x0002, 0, 0, 0, UIntPtr.Zero);
            PumpNetworkInput();
            SetCursorPos((int)signedStart.X - 120, (int)signedStart.Y - 100);
            PumpNetworkInput();
            mouse_event(0x0004, 0, 0, 0, UIntPtr.Zero);
            PumpNetworkInput();
            var signedEnd = border.PointToScreen(new Point(25, 25));
            Check(Math.Abs(client.X + 100) < 3 && Math.Abs(client.Y + 80) < 3 &&
                Math.Abs(signedEnd.X - signedStart.X + 120) < 3 && Math.Abs(signedEnd.Y - signedStart.Y + 100) < 3,
                "Dragging above and left of zero must follow the pointer without a wall or viewport jump");
            Check(store.Load()[client.Key].X < 0 && store.Load()[client.Key].Y < 0,
                "Negative dragged coordinates must be saved");

            view.Zoom = 0.1;
            FlushLayout(window);
            Check(scroll.ScrollableWidth > 0 && scroll.ScrollableHeight > 0,
                "A map smaller than the viewport must still allow panning");
            scroll.ScrollToHorizontalOffset(double.MaxValue);
            scroll.ScrollToVerticalOffset(double.MaxValue);
            FlushLayout(window);
            Check(scroll.HorizontalOffset == scroll.ScrollableWidth && scroll.VerticalOffset == scroll.ScrollableHeight &&
                scroll.HorizontalOffset < canvas.ActualWidth * view.Zoom + scroll.ViewportWidth + 1,
                "Panning must stop at the padded map boundaries");
            scroll.ScrollToHorizontalOffset(-100000);
            scroll.ScrollToVerticalOffset(-100000);
            FlushLayout(window);
            Check(scroll.HorizontalOffset == 0 && scroll.VerticalOffset == 0, "Panning must stop at the opposite boundaries");
            view.Zoom = 100;
            Check(view.Zoom == 2, "Zoom must have a reasonable upper bound");
            view.Zoom = -100;
            Check(view.Zoom == 0.1, "Zoom must have a reasonable lower bound");
            ((Button)view.FindName("ActualSizeButton")).RaiseEvent(new RoutedEventArgs(Button.ClickEvent));
            FlushLayout(window);
            Check(view.Zoom == 1 && zoomLevel.Text == view.Zoom.ToString("P0"), "100% must restore the original zoom");

            foreach (var (text, ui) in new[] { (10, 10), (10, 28), (28, 10), (28, 28), (14, 14) })
            {
                AppearanceSettings.Current.TextFontSize = text;
                AppearanceSettings.Current.InterfaceFontSize = ui;
                FlushLayout(window);
                Check(border.ActualWidth == client.Width && border.ActualHeight == client.Height,
                    "Node bounds must follow font settings");
                var labels = VisualChildren<TextBlock>(border).Where(t => t.IsVisible).ToList();
                Check(labels.Count == 5 && labels.All(t => t.ActualHeight > 0 &&
                    t.TranslatePoint(new Point(0, t.ActualHeight), border).Y < border.ActualHeight),
                    "Names, addresses, connection and filtration labels must fit at mixed font sizes");
                CheckControlsFit(view, "network toolbar");
            }
            model.Move(model.Server, 80, 320);
            FlushLayout(window);
            var serverPosition = Node(model.Server).TranslatePoint(new Point(), canvas);
            Check(Math.Abs(serverPosition.X - model.Server.X + model.CanvasLeft) < 0.1 && Math.Abs(serverPosition.Y - model.Server.Y + model.CanvasTop) < 0.1,
                "Moving the server must update its displayed position");
            ((Button)view.FindName("FitButton")).RaiseEvent(new RoutedEventArgs(Button.ClickEvent));
            FlushLayout(window);
            Check(view.Zoom < 1, "Fit must reduce the zoom when the map exceeds the viewport");
            model.SetAvailable(false);
            FlushLayout(window);
            Check(Node(client).IsVisible && VisualChildren<TextBlock>(border).Any(t => t.Text == "Статус неизвестен") &&
                Equals(status.Foreground, Application.Current.FindResource("TextDim")) && filtration.Text == "Фильтрация: Нет данных",
                "Backend loss must retain the map and show unknown status");
            model.SetAvailable(true);
            model.Refresh([online, offline, pending], true);
            online.SetOnline(false);
            model.Refresh([online, offline, pending], true);
            FlushLayout(window);
            Check(Equals(border.BorderBrush, Application.Current.FindResource("StatusDisconnected")),
                "Disconnect must turn an existing node border red, including while focused");
            online.SetOnline(true);
            model.Refresh([online, offline, pending], true);
            FlushLayout(window);
            Check(Equals(border.BorderBrush, Application.Current.FindResource("StatusRunning")),
                "Reconnect must turn the existing node border green");
            ((Button)view.FindName("ResetButton")).RaiseEvent(new RoutedEventArgs(Button.ClickEvent));
            FlushLayout(window);
            Check(model.Server.X == 350 && model.Server.Y == 60 && store.Load()["server"].X == 350,
                "Reset must restore and save the default arrangement");
            if (Environment.GetEnvironmentVariable("BIGBROTHER_NETWORK_SCREENSHOT") is { Length: > 0 } path)
            {
                view.Zoom = 0.9;
                FlushLayout(window);
                var bitmap = new RenderTargetBitmap((int)view.ActualWidth, (int)view.ActualHeight, 96, 96, PixelFormats.Pbgra32);
                bitmap.Render(view);
                var encoder = new PngBitmapEncoder();
                encoder.Frames.Add(BitmapFrame.Create(bitmap));
                using var file = File.Create(path);
                encoder.Save(file);
            }
            Console.WriteLine("PASS: network map rendering, Ctrl-wheel zoom, bounded background panning, node dragging, keyboard movement, live status labels and font scaling");
        }
        finally
        {
            mouse_event(0x0004, 0, 0, 0, UIntPtr.Zero);
            keybd_event(0x11, 0, 2, UIntPtr.Zero);
            window.Close();
            AppearanceSettings.Current.TextFontSize = textSize;
            AppearanceSettings.Current.InterfaceFontSize = interfaceSize;
            if (Directory.Exists(directory)) Directory.Delete(directory, recursive: true);
        }
    }

    private static void PumpNetworkInput()
    {
        var frame = new DispatcherFrame();
        var timer = new DispatcherTimer { Interval = TimeSpan.FromMilliseconds(80) };
        timer.Tick += (_, _) => { timer.Stop(); frame.Continue = false; };
        timer.Start();
        Dispatcher.PushFrame(frame);
    }

    private static void TestOfflineNavigation()
    {
        string layoutPath = System.IO.Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
            "BigBrother", "Server", "network-layout.json");
        byte[]? previousLayout = File.Exists(layoutPath) ? File.ReadAllBytes(layoutPath) : null;
        MainWindow? window = null;
        try
        {
            window = new MainWindow { WindowState = WindowState.Normal, Width = 1180, Height = 800 };
            var model = (MainViewModel)window.DataContext;
            model.Dispose();
            window.Show();
            FlushLayout(window);
            var tabs = VisualChildren<RadioButton>(window).Where(tab => tab.GroupName == "Nav").ToList();
            Check(tabs.Count == 5 && tabs.All(tab => !tab.IsEnabled),
                "Every menu tab, including Main and Network, must be disabled while offline");
            model.SelectedSection = NavigationSection.Network;
            Check(model.SelectedSection == NavigationSection.Main, "Offline navigation must reject opening the network scheme");
            model.IsConnected = true;
            model.SelectedSection = NavigationSection.Network;
            FlushLayout(window);
            Check(tabs.All(tab => tab.IsEnabled) && VisualChildren<NetworkSchemeView>(window).Single().IsVisible,
                "Reconnection must enable all tabs and allow opening the network scheme");
            model.IsConnected = false;
            FlushLayout(window);
            Check(model.SelectedSection == NavigationSection.Main && tabs.All(tab => !tab.IsEnabled) &&
                !VisualChildren<NetworkSchemeView>(window).Single().IsVisible,
                "Backend loss must leave the network scheme and disable every menu tab");
            Console.WriteLine("PASS: offline menu gating, network navigation rejection, reconnect and disconnect");
        }
        finally
        {
            window?.Close();
            if (previousLayout != null) File.WriteAllBytes(layoutPath, previousLayout);
            else if (File.Exists(layoutPath)) File.Delete(layoutPath);
        }
    }
}
