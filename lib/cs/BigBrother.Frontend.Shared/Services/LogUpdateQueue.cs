namespace frontend.Services;

public sealed class LogUpdateQueue<T>
{
    private readonly object _gate = new();
    private readonly Queue<T> _pending = new();
    private readonly int _capacity;
    private readonly Action<Action> _schedule;
    private readonly Action<List<T>> _consume;
    private bool _scheduled;

    public LogUpdateQueue(int capacity, Action<Action> schedule, Action<List<T>> consume)
    {
        _capacity = capacity;
        _schedule = schedule;
        _consume = consume;
    }

    public void Add(T entry)
    {
        lock (_gate)
        {
            _pending.Enqueue(entry);
            while (_pending.Count > _capacity) _pending.Dequeue();
            if (_scheduled) return;
            _scheduled = true;
        }
        _schedule(Drain);
    }

    private void Drain()
    {
        List<T> entries;
        lock (_gate)
        {
            entries = new List<T>(_pending);
            _pending.Clear();
        }
        try
        {
            _consume(entries);
        }
        finally
        {
            bool again;
            lock (_gate)
            {
                again = _pending.Count > 0;
                if (!again) _scheduled = false;
            }
            if (again) _schedule(Drain);
        }
    }
}
