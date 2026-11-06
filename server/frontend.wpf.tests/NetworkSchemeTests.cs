using System.IO;
using System.Runtime.InteropServices;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using System.Windows.Shapes;
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

    private static void TestNetworkScheme()
    {
        string directory = System.IO.Path.Combine(System.IO.Path.GetTempPath(), "bb-map-wpf-" + Guid.NewGuid().ToString("N"));
        var store = new NetworkLayoutStore(System.IO.Path.Combine(directory, "layout.json"));
        var model = new NetworkSchemeViewModel(store);
        model.SetServer("Сервер офиса", "192.168.1.10, 2001:db8::10");
        model.SetAvailable(true);
        using var online = new UserOverview(new ConnectedClient { Name = "Бухгалтерия", Address = "192.168.1.21" });
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
            var line = VisualChildren<Line>(view).Single(l => ReferenceEquals(l.DataContext, client));
            Check(Math.Abs(line.X1 - model.Server.CenterX) < 0.1 && Math.Abs(line.X2 - client.CenterX) < 0.1,
                "Connection lines must bind to server and client positions");
            Check(Equals(line.Stroke, Application.Current.FindResource("StatusRunning")) && line.StrokeDashArray == null,
                "Connected clients must have solid green links");
            var offlineLine = VisualChildren<Line>(view).Single(l => l.DataContext is NetworkNode { Name: "Переговорная" });
            Check(Equals(offlineLine.Stroke, Application.Current.FindResource("StatusDisconnected")) && offlineLine.StrokeDashArray is { Count: > 0 },
                "Disconnected clients must have dashed red links");
            border.Focus();
            double previousX = client.X;
            border.RaiseEvent(new KeyEventArgs(Keyboard.PrimaryDevice, PresentationSource.FromVisual(border), 0, Key.Right)
                { RoutedEvent = Keyboard.KeyDownEvent });
            FlushLayout(window);
            Check(client.X == previousX + 10 && store.Load()[client.Key].X == client.X && line.X2 == client.CenterX,
                "Keyboard movement must move the node, save its position and update the line");

            var zoom = (Slider)view.FindName("ZoomSlider");
            foreach (double scale in new[] { 1d, 0.6 })
            {
                zoom.Value = scale;
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
            }

            foreach (var (text, ui) in new[] { (10, 28), (28, 10), (14, 14) })
            {
                AppearanceSettings.Current.TextFontSize = text;
                AppearanceSettings.Current.InterfaceFontSize = ui;
                FlushLayout(window);
                Check(border.ActualWidth == client.Width && border.ActualHeight == client.Height,
                    "Node bounds must follow font settings");
                var labels = VisualChildren<TextBlock>(border).Where(t => t.IsVisible).ToList();
                Check(labels.Count == 4 && labels.All(t => t.ActualHeight > 0 &&
                    t.TranslatePoint(new Point(0, t.ActualHeight), border).Y < border.ActualHeight),
                    "Names, addresses and status labels must fit at mixed font sizes");
                CheckControlsFit(view, "network toolbar");
            }
            model.Move(model.Server, 80, 320);
            FlushLayout(window);
            Check(line.X1 == model.Server.CenterX && line.Y1 == model.Server.CenterY,
                "Moving the server must update connection endpoints");
            ((Button)view.FindName("FitButton")).RaiseEvent(new RoutedEventArgs(Button.ClickEvent));
            FlushLayout(window);
            Check(zoom.Value < 1, "Fit must reduce the zoom when the map exceeds the viewport");
            model.SetAvailable(false);
            FlushLayout(window);
            Check(Node(client).IsVisible && VisualChildren<TextBlock>(border).Any(t => t.Text == "Статус неизвестен") &&
                Equals(line.Stroke, Application.Current.FindResource("TextDim")) && line.StrokeDashArray is { Count: > 0 },
                "Backend loss must retain the map and show unknown status");
            model.SetAvailable(true);
            model.Refresh([online, offline, pending], true);
            ((Button)view.FindName("ResetButton")).RaiseEvent(new RoutedEventArgs(Button.ClickEvent));
            FlushLayout(window);
            Check(model.Server.X == 380 && model.Server.Y == 60 && store.Load()["server"].X == 380,
                "Reset must restore and save the default arrangement");
            if (Environment.GetEnvironmentVariable("BIGBROTHER_NETWORK_SCREENSHOT") is { Length: > 0 } path)
            {
                zoom.Value = 0.9;
                FlushLayout(window);
                var bitmap = new RenderTargetBitmap((int)view.ActualWidth, (int)view.ActualHeight, 96, 96, PixelFormats.Pbgra32);
                bitmap.Render(view);
                var encoder = new PngBitmapEncoder();
                encoder.Frames.Add(BitmapFrame.Create(bitmap));
                using var file = File.Create(path);
                encoder.Save(file);
            }
            Console.WriteLine("PASS: network map rendering, pointer dragging at two zoom levels, keyboard movement, live lines, font scaling and controls");
        }
        finally
        {
            mouse_event(0x0004, 0, 0, 0, UIntPtr.Zero);
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
}
