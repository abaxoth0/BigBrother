using System.Windows.Input;
using frontend.Models;

namespace frontend.ViewModels;

public class WhitelistPresetMenuViewModel : ViewModelBase
{
    private List<WhitelistPreset> _presets = new();
    private string _searchText = "";
    private bool _isLoading;
    private string _error = "";
    private ICommand? _loadPresetCommand;

    public ICommand? LoadPresetCommand { get => _loadPresetCommand; set => SetProperty(ref _loadPresetCommand, value); }
    public List<WhitelistPreset> VisiblePresets => _presets
        .Where(p => p.Name.Contains(SearchText.Trim(), StringComparison.CurrentCultureIgnoreCase)).ToList();
    public string Status => _isLoading ? "Загрузка…" : _error.Length > 0 ? _error
        : _presets.Count == 0 ? "Пресетов пока нет. Сохраните текущий выбор списков."
        : VisiblePresets.Count == 0 ? "Пресеты не найдены." : "";

    public string SearchText
    {
        get => _searchText;
        set { if (SetProperty(ref _searchText, value)) NotifyResults(); }
    }

    public void BeginLoading()
    {
        _isLoading = true;
        _error = "";
        _presets.Clear();
        NotifyResults();
    }

    public void SetPresets(IEnumerable<WhitelistPreset> presets)
    {
        _presets = presets.OrderBy(p => p.Name, StringComparer.CurrentCultureIgnoreCase).ToList();
        _isLoading = false;
        _error = "";
        NotifyResults();
    }

    public void RemovePreset(string name)
    {
        _presets.RemoveAll(preset => preset.Name == name);
        NotifyResults();
    }

    public void SetError(string error)
    {
        _isLoading = false;
        _error = error;
        NotifyResults();
    }

    private void NotifyResults()
    {
        OnPropertyChanged(nameof(VisiblePresets));
        OnPropertyChanged(nameof(Status));
    }
}
