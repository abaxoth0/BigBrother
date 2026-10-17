using System.Windows;
using frontend.Services;

namespace frontend;

public partial class App : System.Windows.Application
{
    private bool _installingUpdate;

    protected override void OnStartup(StartupEventArgs e)
    {
        if (!ElevationHelper.IsAdmin())
        {
            ElevationHelper.TryElevate();
            Shutdown(1);
            return;
        }

        base.OnStartup(e);
        if (e.Args.Length == 2 && e.Args[0] == "--apply-update")
        {
            var progress = new UpdateProgressWindow();
            MainWindow = progress;
            progress.Show();
            _ = ApplyUpdateAsync(e.Args[1], progress);
            return;
        }
        MainWindow = new MainWindow();
        MainWindow.Show();
        _ = RecoverInterruptedUpdateAsync();
    }

    private static async Task ApplyUpdateAsync(string work, UpdateProgressWindow progress)
    {
        try
        {
            await Task.Run(() => ClientUpdates.ApplyAsync(work, progress.Report));
            progress.Finish("Обновление завершено. Клиент можно открыть снова.");
        }
        catch (Exception ex)
        {
            progress.Finish("Не удалось обновить клиент.\n" + ex.Message);
        }
    }

    private async Task RecoverInterruptedUpdateAsync()
    {
        try
        {
            var work = await Task.Run(ClientUpdates.PendingRecovery);
            if (work == null) return;
            ClientUpdates.Launch(work);
            Shutdown();
        }
        catch (Exception ex)
        {
            var progress = new UpdateProgressWindow();
            progress.Show();
            progress.Finish("Не удалось восстановить прерванное обновление.\n" + ex.Message);
        }
    }

    public async Task InstallUpdateAsync(ClientUpdate update)
    {
        if (_installingUpdate) return;
        _installingUpdate = true;
        var progress = new UpdateProgressWindow();
        progress.Show();
        try
        {
            using var ipc = new IpcService();
            var work = await Task.Run(() => ClientUpdates.PrepareAsync(ipc, update, progress.Report));
            ClientUpdates.Launch(work);
            progress.Dismiss();
            Shutdown();
        }
        catch
        {
            progress.Dismiss();
            throw;
        }
        finally { _installingUpdate = false; }
    }
}
