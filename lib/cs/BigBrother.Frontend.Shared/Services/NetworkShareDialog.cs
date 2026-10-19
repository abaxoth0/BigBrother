using System.Windows;
using System.Windows.Interop;

namespace frontend.Services;

public static class NetworkShareDialog
{
    public static async Task ConnectAsync(string share)
    {
        await Application.Current.Dispatcher.InvokeAsync(() => {
            var owner = Application.Current.Windows.OfType<Window>().FirstOrDefault(w => w.IsActive) ?? Application.Current.MainWindow;
            NetworkShareAccess.Connect(share, owner == null ? IntPtr.Zero : new WindowInteropHelper(owner).Handle);
        });
    }
}
