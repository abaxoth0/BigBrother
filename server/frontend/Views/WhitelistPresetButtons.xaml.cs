using System.Windows;
using System.Windows.Controls;
using frontend.Models;
using frontend.ViewModels;

namespace frontend.Views;

public partial class WhitelistPresetButtons : UserControl
{
    private bool _loading;
    private readonly WhitelistPresetMenuViewModel _menu;

    public WhitelistPresetButtons()
    {
        InitializeComponent();
        _menu = (WhitelistPresetMenuViewModel)Resources["PresetMenu"];
        Unloaded += (_, _) => PresetPopup.IsOpen = false;
    }

    private async void Load_Click(object sender, RoutedEventArgs e)
    {
        if (_loading || DataContext is not MainViewModel vm || !vm.CanUseWhitelistPresets) return;
        _loading = true;
        _menu.LoadPresetCommand = vm.LoadWhitelistPresetCommand;
        _menu.BeginLoading();
        PresetPopup.IsOpen = true;
        PresetSearch.Focus();
        try
        {
            var presets = await vm.GetWhitelistPresetsAsync();
            _menu.SetPresets(presets);
        }
        catch (Exception ex) { _menu.SetError($"Не удалось загрузить пресеты.\n{ex.Message}"); }
        finally { _loading = false; }
    }

    private void Preset_Click(object sender, RoutedEventArgs e) => PresetPopup.IsOpen = false;

    private void Preview_Click(object sender, RoutedEventArgs e)
    {
        if (sender is not Button { DataContext: WhitelistPreset preset } || DataContext is not MainViewModel vm) return;
        PresetPopup.IsOpen = false;
        var window = new WhitelistPresetPreviewWindow(preset, vm.GetWhitelistPreviewEntriesAsync)
        {
            Owner = Window.GetWindow(this)
        };
        window.ShowDialog();
        if (IsVisible && vm.CanUseWhitelistPresets) PresetPopup.IsOpen = true;
    }
}
