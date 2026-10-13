using System.Windows.Input;
using frontend.Validation;

namespace frontend.ViewModels;

public class WhitelistEditViewModel : ViewModelBase
{
    private string _whitelistName = "";
    private string _entriesText = "";
    private string _originalName = "";
    private bool _isEditMode;
    private WhitelistAnalysis _analysis = WhitelistRules.Analyze("");
    private string _testInput = "";

    public WhitelistEditViewModel()
    {
        SaveCommand = new RelayCommand(_ => Save(), _ => CanSave);
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

    private bool CanSave => !string.IsNullOrWhiteSpace(WhitelistName) && !_analysis.HasErrors;
    public string ValidationSummary => _analysis.Issues.Count == 0
        ? $"Ошибок нет. Правил: {_analysis.Rules.Count}."
        : string.Join("\n", _analysis.Issues);
    public string TestInput
    {
        get => _testInput;
        set
        {
            if (SetProperty(ref _testInput, value)) OnPropertyChanged(nameof(TestResult));
        }
    }
    public string TestResult => string.IsNullOrWhiteSpace(TestInput)
        ? "Введите адрес для проверки текущих правил."
        : WhitelistRules.Test(_analysis, TestInput);

    private void RefreshAnalysis()
    {
        _analysis = WhitelistRules.Analyze(_entriesText);
        OnPropertyChanged(nameof(ValidationSummary));
        OnPropertyChanged(nameof(TestResult));
        (SaveCommand as RelayCommand)?.RaiseCanExecuteChanged();
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

    private void Save()
    {
        if (CanSave) Saved?.Invoke(this, EventArgs.Empty);
    }

    public string GetOriginalName() => _originalName;
    public bool IsEditMode() => _isEditMode;

    public List<string> GetEntries()
    {
        return _analysis.Rules.Select(r => r.Text).ToList();
    }
}
