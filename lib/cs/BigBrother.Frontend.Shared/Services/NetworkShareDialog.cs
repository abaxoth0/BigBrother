using System.Windows;

namespace frontend.Services;

public static class NetworkShareDialog
{
    public static async Task ConnectAsync(string share)
    {
        await Application.Current.Dispatcher.InvokeAsync(() =>
        {
            var dialog = new NetworkCredentialWindow(share) { Owner = DialogWindow.FindOwner() };
            if (dialog.Owner == null) dialog.WindowStartupLocation = WindowStartupLocation.CenterScreen;
            if (dialog.ShowDialog() != true)
                throw new OperationCanceledException("Подключение к общей папке отменено.");
        });
    }
}
