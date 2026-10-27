using System.Windows;
using frontend.Models;
using frontend.ViewModels;

namespace frontend.Views;

public partial class WhitelistTimerWindow : ChromeWindow
{
    private readonly Func<WhitelistTimerViewModel, Task> _save;
    private readonly WhitelistTimerViewModel _viewModel;
    private bool _saving;

    public WhitelistTimerWindow(WhitelistInfo whitelist, Func<WhitelistTimerViewModel, Task> save)
    {
        InitializeComponent();
        WhitelistName.Text = whitelist.Name;
        CurrentTimer.DataContext = whitelist;
        _save = save;
        _viewModel = new WhitelistTimerViewModel(whitelist);
        DataContext = _viewModel;
        Closing += (_, e) => e.Cancel = _saving;
    }

    private async void Save_Click(object sender, RoutedEventArgs e)
    {
        if (_saving || !_viewModel.IsValid) return;
        _saving = true;
        EditorPanel.IsEnabled = false;
        try
        {
            await _save(_viewModel);
            _saving = false;
            DialogResult = true;
        }
        catch (Exception ex)
        {
            MessageBox.Show(this, $"Не удалось сохранить таймер.\n{ex.Message}", "Таймер", MessageBoxButton.OK, MessageBoxImage.Error);
        }
        finally { _saving = false; EditorPanel.IsEnabled = true; }
    }
}
