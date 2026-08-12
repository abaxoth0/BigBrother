using System.Windows.Input;

namespace frontend.ViewModels;

public class WhitelistEditViewModel : ViewModelBase
{
    private string _whitelistName = "";
    private string _entriesText = "";
    private string _originalName = "";
    private bool _isEditMode;

    public WhitelistEditViewModel()
    {
        SaveCommand = new RelayCommand(_ => Save(), _ => !string.IsNullOrWhiteSpace(WhitelistName));
    }

    public string WhitelistName
    {
        get => _whitelistName;
        set
        {
            if (SetProperty(ref _whitelistName, value))
                (SaveCommand as RelayCommand)?.RaiseCanExecuteChanged();
        }
    }

    public string EntriesText
    {
        get => _entriesText;
        set => SetProperty(ref _entriesText, value);
    }

    public string WindowTitle => _isEditMode ? "Редактировать список" : "Создать список";

    public ICommand SaveCommand { get; }

    public event EventHandler? Saved;

    public void SetEditMode(string name, List<string> entries)
    {
        _isEditMode = true;
        _originalName = name;
        _whitelistName = name;
        _entriesText = string.Join("\n", entries);
        OnPropertyChanged(nameof(WhitelistName));
        OnPropertyChanged(nameof(EntriesText));
        OnPropertyChanged(nameof(WindowTitle));
    }

    public void SetCreateMode()
    {
        _isEditMode = false;
        _originalName = "";
        _whitelistName = "";
        _entriesText = "";
        OnPropertyChanged(nameof(WhitelistName));
        OnPropertyChanged(nameof(EntriesText));
        OnPropertyChanged(nameof(WindowTitle));
    }

    private void Save()
    {
        Saved?.Invoke(this, EventArgs.Empty);
    }

    public string GetOriginalName() => _originalName;
    public bool IsEditMode() => _isEditMode;

    public List<string> GetEntries()
    {
        return _entriesText
            .Split('\n')
            .Select(e => e.Trim())
            .Where(e => !string.IsNullOrEmpty(e))
            .ToList();
    }
}
