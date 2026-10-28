using frontend.Models;
using frontend.ViewModels;

namespace frontend.Views;

public partial class WhitelistPresetPreviewWindow : ChromeWindow
{
    public WhitelistPresetPreviewWindow(WhitelistPreset preset, Func<string, Task<List<string>>> loadRules)
    {
        InitializeComponent();
        var viewModel = new WhitelistPresetPreviewViewModel(preset, loadRules);
        DataContext = viewModel;
        Closed += (_, _) => viewModel.Close();
    }
}
