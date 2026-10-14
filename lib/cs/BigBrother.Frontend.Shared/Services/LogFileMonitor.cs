using System.IO;

namespace frontend.Services;

internal sealed class LogFileMonitor : IDisposable
{
    private readonly FileSystemWatcher _watcher;
    private readonly SemaphoreSlim _signal = new(0, 1);
    private readonly CancellationTokenSource _stop = new();
    private readonly Task _worker;
    private int _disposed;

    public LogFileMonitor(string directory, string filter, Action read)
    {
        _watcher = new FileSystemWatcher(directory, filter)
        {
            NotifyFilter = NotifyFilters.LastWrite | NotifyFilters.FileName | NotifyFilters.Size,
            InternalBufferSize = 64 * 1024
        };
        _watcher.Changed += (_, _) => Signal();
        _watcher.Created += (_, _) => Signal();
        _watcher.Renamed += (_, _) => Signal();
        _watcher.Error += (_, _) => Signal();
        _worker = Task.Run(async () =>
        {
            try
            {
                while (true)
                {
                    await _signal.WaitAsync(_stop.Token);
                    if (_stop.IsCancellationRequested) break;
                    try { read(); }
                    catch (IOException) { }
                    catch (UnauthorizedAccessException) { }
                }
            }
            catch (OperationCanceledException) { }
        });
        _watcher.EnableRaisingEvents = true;
        Signal();
    }

    private void Signal()
    {
        if (Volatile.Read(ref _disposed) != 0) return;
        try { _signal.Release(); }
        catch (SemaphoreFullException) { }
        catch (ObjectDisposedException) { }
    }

    public void Dispose()
    {
        if (Interlocked.Exchange(ref _disposed, 1) != 0) return;
        _watcher.Dispose();
        _stop.Cancel();
        _ = _worker.ContinueWith(_ =>
        {
            _signal.Dispose();
            _stop.Dispose();
        }, TaskScheduler.Default);
    }
}
