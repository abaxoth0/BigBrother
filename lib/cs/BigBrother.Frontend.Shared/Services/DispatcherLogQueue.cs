using System.Windows.Threading;

namespace frontend.Services;

public sealed class DispatcherLogQueue<T> : IDisposable
{
    private readonly LogUpdateQueue<T> _updates;
    private readonly DispatcherTimer _timer;
    private Action? _pending;
    private int _disposed;

    public DispatcherLogQueue(int capacity, Dispatcher dispatcher, Action<List<T>> consume)
    {
        dispatcher.VerifyAccess();
        _updates = new LogUpdateQueue<T>(capacity,
            drain => Interlocked.Exchange(ref _pending, drain), consume);
        // Normal priority prevents rendering/input from starving log updates.
        _timer = new DispatcherTimer(TimeSpan.FromMilliseconds(100), DispatcherPriority.Normal,
            (_, _) => Interlocked.Exchange(ref _pending, null)?.Invoke(), dispatcher);
    }

    public void Add(T entry)
    {
        if (Volatile.Read(ref _disposed) == 0) _updates.Add(entry);
    }

    public void Dispose()
    {
        if (Interlocked.Exchange(ref _disposed, 1) != 0) return;
        _timer.Stop();
        Interlocked.Exchange(ref _pending, null);
    }
}
