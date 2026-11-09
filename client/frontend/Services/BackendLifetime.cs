using System.Diagnostics;
using System.IO;
using System.ServiceProcess;

namespace frontend.Services;

public sealed class BackendLifetime : IDisposable
{
    private readonly CancellationTokenSource _stop = new();
    private readonly Process _backend;

    private BackendLifetime(Process backend, Action stopped)
    {
        _backend = backend;
        _ = WatchAsync(stopped);
    }

    public static async Task<BackendLifetime> AttachAsync(Action stopped, int? expectedPid = null)
    {
        using var manager = new ServiceManager("BigBrother.Client");
        if (expectedPid == null && !await manager.StartServiceAsync())
            throw new IOException("Не удалось запустить клиентский сервис BigBrother.Client.");
        using var ipc = new IpcService();
        for (int attempt = 0; attempt < 50; ++attempt)
        {
            var status = await ipc.GetStatusAsync();
            if (status.IsConnected && status.ClientPid > 0)
            {
                if (expectedPid != null && status.ClientPid != expectedPid)
                    throw new IOException("Клиентский сервис был перезапущен.");
                return new BackendLifetime(Process.GetProcessById(status.ClientPid), stopped);
            }
            await Task.Delay(100);
        }
        throw new IOException("Клиентский сервис не отвечает.");
    }

    private async Task WatchAsync(Action stopped)
    {
        try
        {
            using var service = new ServiceController("BigBrother.Client");
            while (!_stop.IsCancellationRequested)
            {
                service.Refresh();
                if (_backend.HasExited || service.Status != ServiceControllerStatus.Running) break;
                await Task.Delay(250, _stop.Token).ConfigureAwait(false);
            }
            if (!_stop.IsCancellationRequested) stopped();
        }
        catch (OperationCanceledException) when (_stop.IsCancellationRequested) { }
        catch (Exception) { if (!_stop.IsCancellationRequested) stopped(); }
    }

    public void Dispose()
    {
        _stop.Cancel();
    }
}
