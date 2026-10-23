using frontend.Models;
using frontend.Validation;

namespace frontend.ViewModels;

public class DomainCheckViewModel : ViewModelBase
{
    private readonly WhitelistAnalysis _analysis;
    private readonly Dictionary<string, List<string>> _sources;
    private string _testInput = "";

    public DomainCheckViewModel(IEnumerable<ComposedWhitelistEntry> entries)
    {
        var rules = entries.ToList();
        _analysis = WhitelistRules.Analyze(string.Join("\n", rules.Select(r => r.Value)));
        _sources = rules.ToDictionary(r => r.Value, r => r.Sources, StringComparer.OrdinalIgnoreCase);
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
        ? "Введите домен или IPv4, при необходимости с :портом."
        : WhitelistRules.Test(_analysis, TestInput, rule =>
            $"{rule.Text} — списки: {string.Join(", ", _sources[rule.Text].Select(name => $"«{name}»"))}");
}
