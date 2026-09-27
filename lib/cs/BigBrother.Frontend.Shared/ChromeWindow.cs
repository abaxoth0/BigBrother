using System;
using System.ComponentModel;
using System.Windows;
using System.Windows.Interop;

namespace frontend;

/// <summary>
/// Base window for custom-chrome windows: provides the native message hook to
/// suppress double-click-to-maximize and shared minimize/close handlers.
/// </summary>
public class ChromeWindow : Window
{
    private const int WM_NCLBUTTONDBLCLK = 0x00A3;
    private HwndSource? _hwndSource;

    protected ChromeWindow()
    {
        SourceInitialized += OnSourceInitialized;
    }

    private void OnSourceInitialized(object? sender, EventArgs e)
    {
        _hwndSource = HwndSource.FromHwnd(new WindowInteropHelper(this).Handle);
        _hwndSource?.AddHook(WndProc);
    }

    private IntPtr WndProc(IntPtr hwnd, int msg, IntPtr wParam, IntPtr lParam, ref bool handled)
    {
        if (msg == WM_NCLBUTTONDBLCLK)
        {
            handled = true;
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