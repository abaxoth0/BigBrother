using System.Windows.Input;
using frontend.Models;

namespace frontend.ViewModels;

public class WhitelistPresetPreviewViewModel : ViewModelBase
{
    private readonly Func<string, Task<List<string>>> _loadRules;
    private readonly List<string> _whitelists;
    private string _searchText = "";
    private string? _selectedWhitelist;
    private string _rulesText = "";
    private string _rulesStatus = "Выберите список для просмотра правил.";
    private int _requestRevision;
    private bool _closed;

    public WhitelistPresetPreviewViewModel(WhitelistPreset preset, Func<string, Task<List<string>>> loadRules)
    {
        Name = preset.Name;
        _whitelists = preset.ActiveWhitelists.OrderBy(name => name, StringComparer.CurrentCultureIgnoreCase).ToList();
        _loadRules = loadRules;
        RefreshRulesCommand = new RelayCommand(_ => RulesLoadTask = LoadRulesAsync(), _ => !_closed && SelectedWhitelist != null);
        SelectedWhitelist = _whitelists.FirstOrDefault();
    }

    public string Name { get; }
    public string WhitelistCountText => $"Активных списков: {_whitelists.Count}";
    public List<string> VisibleWhitelists => _whitelists
        .Where(name => name.Contains(SearchText.Trim(), StringComparison.CurrentCultureIgnoreCase)).ToList();
    public string ListStatus => _whitelists.Count == 0 ? "В пресете нет активных списков."
        : VisibleWhitelists.Count == 0 ? "Списки не найдены." : "";
    public ICommand RefreshRulesCommand { get; }
    public Task RulesLoadTask { get; private set; } = Task.CompletedTask;

    public string SearchText
    {
        get => _searchText;
        set
        {
            if (!SetProperty(ref _searchText, value)) return;
            OnPropertyChanged(nameof(VisibleWhitelists));
            OnPropertyChanged(nameof(ListStatus));
            if (!VisibleWhitelists.Contains(SelectedWhitelist ?? "")) SelectedWhitelist = null;
        }
    }

    public string? SelectedWhitelist
    {
        get => _selectedWhitelist;
        set
        {
            if (SetProperty(ref _selectedWhitelist, value)) RulesLoadTask = LoadRulesAsync();
        }
    }

    public string RulesText { get => _rulesText; private set => SetProperty(ref _rulesText, value); }
    public string RulesStatus { get => _rulesStatus; private set => SetProperty(ref _rulesStatus, value); }

    private async Task LoadRulesAsync()
    {
        var revision = ++_requestRevision;
        var name = SelectedWhitelist;
        RulesText = "";
        RulesStatus = name == null ? "Выберите список для просмотра правил." : "Загрузка правил…";
        if (name == null || _closed) return;
        try
        {
            var entries = await _loadRules(name);
            if (_closed || revision != _requestRevision) return;
            RulesText = string.Join("\n", entries);
            RulesStatus = entries.Count == 0 ? "Список пуст." : "";
        }
        catch (Exception ex)
        {
            if (!_closed && revision == _requestRevision) RulesStatus = $"Не удалось загрузить правила.\n{ex.Message}";
        }
    }

    public void Close()
    {
        _closed = true;
        _requestRevision++;
    }
}
