using System.Windows;
using System.Windows.Controls;
using frontend.ViewModels;

namespace frontend.Views;

public partial class WhitelistPresetButtons : UserControl
{
    private bool _loading;

    public WhitelistPresetButtons()
    {
        InitializeComponent();
        Unloaded += (_, _) => PresetPopup.IsOpen = false;
    }

    private async void Load_Click(object sender, RoutedEventArgs e)
    {
        if (_loading || DataContext is not MainViewModel vm || !vm.CanUseWhitelistPresets) return;
        _loading = true;
        Presets.DataContext = vm;
        Presets.ItemsSource = null;
        Status.Text = "Загрузка…";
        PresetPopup.IsOpen = true;
        try
        {
            var presets = await vm.GetWhitelistPresetsAsync();
            Presets.ItemsSource = presets;
            Status.Text = presets.Count == 0 ? "Пресетов пока нет. Сохраните текущий выбор списков." : "";
        }
        catch (Exception ex) { Status.Text = $"Не удалось загрузить пресеты.\n{ex.Message}"; }
        finally { _loading = false; }
    }

    private void Preset_Click(object sender, RoutedEventArgs e) => PresetPopup.IsOpen = false;
}
