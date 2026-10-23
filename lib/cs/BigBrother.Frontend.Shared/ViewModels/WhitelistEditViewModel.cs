using System.Windows.Input;
using frontend.Validation;

namespace frontend.ViewModels;

public sealed class WhitelistSaveEventArgs(bool closeAfterSave) : EventArgs
{
    public bool CloseAfterSave { get; } = closeAfterSave;
}

public class WhitelistEditViewModel : ViewModelBase
{
    private string _whitelistName = "";
    private string _entriesText = "";
    private string _originalName = "";
    private bool _isEditMode;
    private bool _isSaving;
    private WhitelistAnalysis _analysis = WhitelistRules.Analyze("");

    public WhitelistEditViewModel()
    {
        SaveCommand = new RelayCommand(_ => Save(true), _ => CanSave);
        SaveInPlaceCommand = new RelayCommand(_ => Save(false), _ => CanSave);
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
        set
        {
            if (SetProperty(ref _entriesText, value)) RefreshAnalysis();
        }
    }

    public bool IsSaving
    {
        get => _isSaving;
        set
        {
            if (!SetProperty(ref _isSaving, value)) return;
            OnPropertyChanged(nameof(CanEdit));
            (SaveCommand as RelayCommand)?.RaiseCanExecuteChanged();
        }
    }

    public bool CanEdit => !IsSaving;
    private bool CanSave => !IsSaving && !string.IsNullOrWhiteSpace(WhitelistName) && !_analysis.HasErrors;
    public IReadOnlyList<WhitelistIssue> ValidationIssues => _analysis.Issues;
    public string ValidationSummary => _analysis.Issues.Count == 0
        ? $"Ошибок нет. Правил: {_analysis.Rules.Count}."
        : string.Join("\n", _analysis.Issues);

    private void RefreshAnalysis()
    {
        _analysis = WhitelistRules.Analyze(_entriesText);
        OnPropertyChanged(nameof(ValidationIssues));
        OnPropertyChanged(nameof(ValidationSummary));
        (SaveCommand as RelayCommand)?.RaiseCanExecuteChanged();
    }

    public string WindowTitle => _isEditMode ? "Редактировать список" : "Создать список";

    public ICommand SaveCommand { get; }
    public ICommand SaveInPlaceCommand { get; }

    public event EventHandler<WhitelistSaveEventArgs>? Saved;

    public void SetEditMode(string name, List<string> entries)
    {
        _isEditMode = true;
        _originalName = name;
        _whitelistName = name;
        _entriesText = string.Join("\n", entries);
        RefreshAnalysis();
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
        RefreshAnalysis();
        OnPropertyChanged(nameof(WhitelistName));
        OnPropertyChanged(nameof(EntriesText));
        OnPropertyChanged(nameof(WindowTitle));
    }

    public void MarkSaved(string name)
    {
        _originalName = name;
        _isEditMode = true;
        OnPropertyChanged(nameof(WindowTitle));
    }

    private void Save(bool closeAfterSave)
    {
        if (CanSave) Saved?.Invoke(this, new WhitelistSaveEventArgs(closeAfterSave));
    }

    public string GetOriginalName() => _originalName;
    public bool IsEditMode() => _isEditMode;

    public List<string> GetEntries()
    {
        return _entriesText.Split('\n').Select(line => line.Trim())
            .Where(line => line.Length > 0).ToList();
    }
}
