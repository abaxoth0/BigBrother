using System.Windows;
using frontend.Models;

namespace frontend.Views;

public partial class WhitelistPresetWindow : ChromeWindow
{
    private readonly Func<string, bool, Task> _save;
    private readonly HashSet<string> _existing;
    private bool _saving;

    public WhitelistPresetWindow(IEnumerable<WhitelistPreset> presets, WhitelistPreset selection, Func<string, bool, Task> save)
    {
        InitializeComponent();
        _save = save;
        _existing = presets.Select(p => p.Name).ToHashSet();
        PresetName.ItemsSource = _existing.OrderBy(name => name, StringComparer.CurrentCultureIgnoreCase);
        Preview.Text = selection.Preview;
        Loaded += (_, _) => PresetName.Focus();
        Closing += (_, e) => e.Cancel = _saving;
    }

    private async void Save_Click(object sender, RoutedEventArgs e)
    {
        var name = PresetName.Text.Trim();
        if (_saving) return;
        if (string.IsNullOrWhiteSpace(name) || name.IndexOfAny(new[] { '\r', '\n', '\0' }) >= 0)
        {
            MessageBox.Show(this, "Введите название пресета без переносов строк.", "Сохранить пресет", MessageBoxButton.OK, MessageBoxImage.Information);
            PresetName.Focus();
            return;
        }
        var replace = _existing.Contains(name);
        if (replace && MessageBox.Show(this, $"Заменить пресет «{name}» текущим выбором списков?", "Сохранить пресет",
            MessageBoxButton.YesNo, MessageBoxImage.Question) != MessageBoxResult.Yes) return;
        _saving = true;
        EditorPanel.IsEnabled = false;
        try
        {
            await _save(name, replace);
            _saving = false;
            DialogResult = true;
        }
        catch (Exception ex)
        {
            MessageBox.Show(this, $"Не удалось сохранить пресет.\n{ex.Message}", "Сохранить пресет", MessageBoxButton.OK, MessageBoxImage.Error);
        }
        finally { _saving = false; EditorPanel.IsEnabled = true; }
    }
}
