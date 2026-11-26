using System.Windows.Input;

namespace frontend.ViewModels;

public sealed class ProcessBlacklistViewModel : ViewModelBase
{
    private readonly Func<Task<List<string>>> _load;
    private readonly Func<string[], Task> _save;
    private string? _savedText;
    private string _text = "";
    private string _status = "";
    private bool _busy;

    public ProcessBlacklistViewModel(Func<Task<List<string>>> load, Func<string[], Task> save)
    {
        _load = load;
        _save = save;
        SaveCommand = new RelayCommand(async _ => await SaveAsync(), _ => CanEdit && HasChanges);
        CancelCommand = new RelayCommand(_ => Text = _savedText!, _ => CanEdit && HasChanges);
        RetryCommand = new RelayCommand(async _ => await LoadAsync(), _ => CanRetryLoad);
    }

    public ICommand SaveCommand { get; }
    public ICommand CancelCommand { get; }
    public ICommand RetryCommand { get; }
    public bool CanEdit => !_busy && _savedText != null;
    public bool CanRetryLoad => !_busy && _savedText == null;
    private bool HasChanges => _savedText != null && Text != _savedText;

    public string Text
    {
        get => _text;
        set
        {
            if (SetProperty(ref _text, value))
            {
                Status = "";
                CommandManager.InvalidateRequerySuggested();
            }
        }
    }

    public string Status
    {
        get => _status;
        private set => SetProperty(ref _status, value);
    }

    public async Task LoadAsync()
    {
        if (_busy || HasChanges) return;

        _savedText = null;
        SetBusy(true);
        Status = "Загрузка…";
        try
        {
            _savedText = string.Join(Environment.NewLine, await _load());
            Text = _savedText;
            Status = "";
        }
        catch (Exception ex)
        {
            Status = ex.Message;
        }
        finally
        {
            SetBusy(false);
        }
    }

    private async Task SaveAsync()
    {
        if (!CanEdit || !HasChanges) return;

        string draft = Text;
        SetBusy(true);
        Status = "Сохранение…";
        try
        {
            await _save(draft.ReplaceLineEndings("\n").Split('\n'));
            _savedText = draft;
            Status = "Изменения сохранены.";
        }
        catch (Exception ex)
        {
            Status = ex.Message;
        }
        finally
        {
            SetBusy(false);
        }
    }

    private void SetBusy(bool busy)
    {
        _busy = busy;
        OnPropertyChanged(nameof(CanEdit));
        OnPropertyChanged(nameof(CanRetryLoad));
        CommandManager.InvalidateRequerySuggested();
    }
}
