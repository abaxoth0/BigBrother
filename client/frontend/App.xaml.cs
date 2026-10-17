using System.Windows;
using frontend.Services;

namespace frontend;

public partial class App : System.Windows.Application
{
    private readonly System.Windows.Threading.DispatcherTimer _updateTimer = new() { Interval = TimeSpan.FromMinutes(5) };
    private bool _checkingUpdates;
    private string _lastUpdateError = "";

    protected override void OnStartup(StartupEventArgs e)
    {
        if (!ElevationHelper.IsAdmin())
        {
            ElevationHelper.TryElevate();
            Shutdown(1);
            return;
        }

        StartupUri = null;
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
        _updateTimer.Tick += async (_, _) => await CheckForUpdatesAsync();
        _updateTimer.Start();
        _ = CheckForUpdatesAsync();
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

    public async Task CheckForUpdatesAsync()
    {
        if (_checkingUpdates) return;
        _checkingUpdates = true;
        UpdateProgressWindow? progress = null;
        try
        {
            var work = await Task.Run(ClientUpdates.PendingRecovery);
            if (work == null)
            {
                using var ipc = new IpcService();
                work = await Task.Run(() => ClientUpdates.PrepareAsync(ipc, text => Dispatcher.Invoke(() => {
                    if (progress == null) { progress = new UpdateProgressWindow(); progress.Show(); }
                    progress.Report(text);
                })));
            }
            if (work == null) { _lastUpdateError = ""; return; }
            ClientUpdates.Launch(work);
            progress?.Dismiss();
            Shutdown();
        }
        catch (Exception ex)
        {
            if (progress != null || _lastUpdateError != ex.Message)
            {
                if (progress == null) { progress = new UpdateProgressWindow(); progress.Show(); }
                progress.Finish("Не удалось подготовить обновление.\n" + ex.Message);
            }
            _lastUpdateError = ex.Message;
        }
        finally { _checkingUpdates = false; }
    }
}
