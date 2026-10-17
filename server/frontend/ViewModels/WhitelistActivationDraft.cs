using frontend.Models;

namespace frontend.ViewModels;

public class WhitelistActivationDraft
{
    private Dictionary<string, bool> _saved = new();
    private readonly Dictionary<string, bool> _pending = new();

    public bool HasChanges => _pending.Count > 0;

    public void Refresh(IEnumerable<WhitelistInfo> whitelists)
    {
        var rows = whitelists.ToList();
        _saved = rows.ToDictionary(w => w.Name, w => w.IsActive);
        foreach (var (name, active) in _pending.ToList())
            if (!_saved.TryGetValue(name, out var saved) || saved == active) _pending.Remove(name);
        foreach (var row in rows) row.IsActive = GetActive(row.Name);
    }

    public bool GetActive(string name) => _pending.TryGetValue(name, out var active)
        ? active : _saved.GetValueOrDefault(name);

    public void Toggle(WhitelistInfo whitelist)
    {
        var active = !GetActive(whitelist.Name);
        if (active == _saved.GetValueOrDefault(whitelist.Name)) _pending.Remove(whitelist.Name);
        else _pending[whitelist.Name] = active;
        whitelist.IsActive = active;
    }

    public void Cancel(IEnumerable<WhitelistInfo> whitelists)
    {
        _pending.Clear();
        foreach (var row in whitelists) row.IsActive = GetActive(row.Name);
    }

    public Dictionary<string, bool> GetChanges() => new(_pending);

    public void Accept(IReadOnlyDictionary<string, bool> states)
    {
        foreach (var (name, active) in states)
        {
            _saved[name] = active;
            if (_pending.TryGetValue(name, out var pending) && pending == active) _pending.Remove(name);
        }
    }

    public void Rename(string oldName, string newName)
    {
        if (_saved.Remove(oldName, out var saved)) _saved[newName] = saved;
        if (_pending.Remove(oldName, out var pending)) _pending[newName] = pending;
    }
}
