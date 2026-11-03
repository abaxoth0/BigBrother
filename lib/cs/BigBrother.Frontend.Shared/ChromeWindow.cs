using System;
using System.Runtime.InteropServices;
using System.Windows;
using System.Windows.Interop;
using System.Windows.Shell;

namespace frontend;

/// <summary>
/// Base window for custom-chrome windows: provides the native message hook to
/// share caption actions and keep maximized windows inside the monitor work area.
/// </summary>
public class ChromeWindow : Window
{
    private const int WM_NCLBUTTONDBLCLK = 0x00A3;
    private const int WM_GETMINMAXINFO = 0x0024;
    private HwndSource? _hwndSource;

    protected ChromeWindow()
    {
        SetResourceReference(FontSizeProperty, "FontSize14");
        SetResourceReference(BorderBrushProperty, "WindowBorderBrush");
        BorderThickness = new Thickness(1);
        SnapsToDevicePixels = true;
        SourceInitialized += OnSourceInitialized;
    }

    private void OnSourceInitialized(object? sender, EventArgs e)
    {
        UpdateWindowBorder();
        if (WindowChrome.GetWindowChrome(this) is { } chrome)
            chrome.CornerRadius = new CornerRadius(0);
        _hwndSource = HwndSource.FromHwnd(new WindowInteropHelper(this).Handle);
        _hwndSource?.AddHook(WndProc);
    }

    protected override void OnStateChanged(EventArgs e)
    {
        UpdateWindowBorder();
        base.OnStateChanged(e);
    }

    private void UpdateWindowBorder() =>
        BorderThickness = WindowState == WindowState.Maximized ? new Thickness(0) : new Thickness(1);

    private IntPtr WndProc(IntPtr hwnd, int msg, IntPtr wParam, IntPtr lParam, ref bool handled)
    {
        if (msg == WM_NCLBUTTONDBLCLK)
        {
            handled = true;
        }
        else if (msg == WM_GETMINMAXINFO)
        {
            var monitor = MonitorFromWindow(hwnd, 2);
            var info = new MonitorInfo { Size = Marshal.SizeOf<MonitorInfo>() };
            if (GetMonitorInfo(monitor, ref info))
            {
                var bounds = Marshal.PtrToStructure<MinMaxInfo>(lParam);
                bounds.MaxPosition.X = info.Work.Left - info.Monitor.Left;
                bounds.MaxPosition.Y = info.Work.Top - info.Monitor.Top;
                bounds.MaxSize.X = info.Work.Right - info.Work.Left;
                bounds.MaxSize.Y = info.Work.Bottom - info.Work.Top;
                var transform = _hwndSource?.CompositionTarget?.TransformToDevice;
                bounds.MinTrackSize.X = Math.Max(bounds.MinTrackSize.X, (int)Math.Ceiling(MinWidth * (transform?.M11 ?? 1)));
                bounds.MinTrackSize.Y = Math.Max(bounds.MinTrackSize.Y, (int)Math.Ceiling(MinHeight * (transform?.M22 ?? 1)));
                if (!double.IsPositiveInfinity(MaxWidth))
                    bounds.MaxTrackSize.X = (int)Math.Ceiling(MaxWidth * (transform?.M11 ?? 1));
                if (!double.IsPositiveInfinity(MaxHeight))
                    bounds.MaxTrackSize.Y = (int)Math.Ceiling(MaxHeight * (transform?.M22 ?? 1));
                Marshal.StructureToPtr(bounds, lParam, false);
                handled = true;
            }
        }
        return IntPtr.Zero;
    }

    protected void MinimizeButton_Click(object sender, RoutedEventArgs e)
    {
        WindowState = WindowState.Minimized;
    }

    protected void CloseButton_Click(object sender, RoutedEventArgs e)
    {
        Close();
    }

    protected void MaximizeRestoreButton_Click(object sender, RoutedEventArgs e)
    {
        if (ResizeMode is ResizeMode.CanResize or ResizeMode.CanResizeWithGrip)
            WindowState = WindowState == WindowState.Maximized ? WindowState.Normal : WindowState.Maximized;
    }

    [StructLayout(LayoutKind.Sequential)]
    private struct Point { public int X, Y; }

    [StructLayout(LayoutKind.Sequential)]
    private struct Rect { public int Left, Top, Right, Bottom; }

    [StructLayout(LayoutKind.Sequential)]
    private struct MinMaxInfo
    {
        public Point Reserved, MaxSize, MaxPosition, MinTrackSize, MaxTrackSize;
    }

    [StructLayout(LayoutKind.Sequential)]
    private struct MonitorInfo
    {
        public int Size;
        public Rect Monitor, Work;
        public uint Flags;
    }

    [DllImport("user32.dll")]
    private static extern IntPtr MonitorFromWindow(IntPtr window, uint flags);

    [DllImport("user32.dll", CharSet = CharSet.Auto)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool GetMonitorInfo(IntPtr monitor, ref MonitorInfo info);

    protected override void OnClosed(EventArgs e)
    {
        if (_hwndSource != null)
        {
            _hwndSource.RemoveHook(WndProc);
            _hwndSource = null;
        }
        base.OnClosed(e);
    }
}
