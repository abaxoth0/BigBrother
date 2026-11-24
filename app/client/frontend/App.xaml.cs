using System.Windows;
using frontend.Services;

namespace frontend;

public partial class App : System.Windows.Application
{
    private bool _installingUpdate;
    private BackendLifetime? _updateWorkerLifetime;

    protected override async void OnStartup(StartupEventArgs e)
    {
        if (!ElevationHelper.IsAdmin())
        {
            ElevationHelper.TryElevate();
            Shutdown(1);
            return;
        }

        AppearanceSettings.Current.Initialize("Client");
        base.OnStartup(e);
        if (e.Args.Length == 2 && e.Args[0] == "--apply-update")
        {
            var progress = new UpdateProgressWindow();
            MainWindow = progress;
            progress.Show();
            _ = ApplyUpdateAsync(e.Args[1], progress);
            return;
        }
        ShutdownMode = ShutdownMode.OnExplicitShutdown;
        try
        {
            int? expectedPid = e.Args.Length == 2 && e.Args[0] == "--prepare-update"
                ? int.Parse(e.Args[1], System.Globalization.CultureInfo.InvariantCulture) : null;
            if (!await ClientStartup.StartAsync(
                expectedPid,
                () => Task.Run(ClientUpdates.PendingRecovery),
                ClientUpdates.Launch,
                async pid => { _updateWorkerLifetime = await BackendLifetime.AttachAsync(
                    () => Dispatcher.BeginInvoke(() => Shutdown()), pid); }))
            {
                Shutdown();
                return;
            }
            if (expectedPid != null)
            {
                var progress = new UpdateProgressWindow();
                MainWindow = progress;
                progress.Show();
                _ = PrepareUpdateAsync(progress);
                return;
            }
        }
        catch (Exception error)
        {
            System.Windows.MessageBox.Show(error.Message, "BigBrother", MessageBoxButton.OK, MessageBoxImage.Error);
            Shutdown(1);
            return;
        }
        ShutdownMode = ShutdownMode.OnLastWindowClose;
        MainWindow = new MainWindow();
        MainWindow.Show();
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

    public async Task InstallUpdateAsync(ClientUpdate update)
    {
        if (_installingUpdate) return;
        _installingUpdate = true;
        try
        {
            using var ipc = new IpcService();
            if (!await ipc.InstallClientUpdateAsync(System.Text.Json.JsonSerializer.Serialize(update)))
                throw new System.IO.IOException(ipc.LastError);
            Shutdown();
        }
        finally { _installingUpdate = false; }
    }

    private async Task PrepareUpdateAsync(UpdateProgressWindow progress)
    {
        try
        {
            using var ipc = new IpcService();
            var approved = await ipc.GetApprovedUpdateAsync()
                ?? throw new System.IO.IOException("Нет подтверждённого обновления.");
            var update = System.Text.Json.JsonSerializer.Deserialize<ClientUpdate>(approved)
                ?? throw new System.IO.IOException("Некорректные сведения об обновлении.");
            var work = await Task.Run(() => ClientUpdates.PrepareAsync(ipc, update, progress.Report, NetworkShareDialog.ConnectAsync));
            ClientUpdates.Launch(work);
            Shutdown();
        }
        catch (Exception error)
        {
            ShutdownMode = ShutdownMode.OnLastWindowClose;
            progress.Finish("Не удалось подготовить обновление.\n" + error.Message);
        }
    }

    protected override void OnExit(ExitEventArgs e)
    {
        _updateWorkerLifetime?.Dispose();
        base.OnExit(e);
    }
}
