using System.Windows.Input;

namespace frontend.ViewModels;

public sealed class ServerPolicySwitch : ViewModelBase
{
    private readonly Func<Task<bool>> _read;
    private readonly Func<bool, Task> _write;
    private readonly Action<string, string> _log;
    private bool? _enabled;
    private bool _saving;
    private int _revision;

    public ServerPolicySwitch(string name, Func<Task<bool>> read, Func<bool, Task> write, Action<string, string> log)
    {
        Name = name;
        _read = read;
        _write = write;
        _log = log;
        ToggleCommand = new RelayCommand(async _ => await ToggleAsync(), _ => _enabled.HasValue && !_saving);
    }

    public string Name { get; }
    public bool IsOn => _enabled == true;
    public string Status => _enabled switch { true => "Вкл", false => "Выкл", null => "Недоступно" };
    public ICommand ToggleCommand { get; }

    public void Disconnect()
    {
        ++_revision;
        _saving = false;
        ShowState(null);
    }

    public async Task RefreshAsync()
    {
        if (_saving) return;

        int revision = ++_revision;
        bool? value;
        try { value = await _read(); }
        catch (Exception) { value = null; }

        if (revision == _revision) ShowState(value);
    }

    private async Task ToggleAsync()
    {
        if (_enabled is not bool enabled || _saving) return;

        _saving = true;
        int revision = ++_revision;
        CommandManager.InvalidateRequerySuggested();

        try
        {
            await _write(!enabled);
            if (revision != _revision) return;
            ShowState(!enabled);
            _log("INFO", $"{Name} — {Status}");
        }
        catch (Exception ex)
        {
            if (revision != _revision) return;
            _log("ERROR", $"{Name} — {ex.Message}");
        }
        finally
        {
            if (revision == _revision)
            {
                _saving = false;
                await RefreshAsync();
                CommandManager.InvalidateRequerySuggested();
            }
        }
    }

    private void ShowState(bool? enabled)
    {
        _enabled = enabled;
        OnPropertyChanged(nameof(IsOn));
        OnPropertyChanged(nameof(Status));
        CommandManager.InvalidateRequerySuggested();
    }
}
