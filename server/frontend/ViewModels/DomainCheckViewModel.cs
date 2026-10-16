using frontend.Validation;

namespace frontend.ViewModels;

public class DomainCheckViewModel : ViewModelBase
{
    private readonly WhitelistAnalysis _analysis;
    private string _testInput = "";

    public DomainCheckViewModel(IEnumerable<string> entries)
    {
        _analysis = WhitelistRules.Analyze(string.Join("\n", entries));
    }

    public string RulesSummary => $"Правил в общем списке: {_analysis.Rules.Count}.";

    public string TestInput
    {
        get => _testInput;
        set
        {
            if (SetProperty(ref _testInput, value)) OnPropertyChanged(nameof(TestResult));
        }
    }

    public string TestResult => string.IsNullOrWhiteSpace(TestInput)
        ? "Введите домен или IPv4 для проверки."
        : WhitelistRules.Test(_analysis, TestInput);
}
