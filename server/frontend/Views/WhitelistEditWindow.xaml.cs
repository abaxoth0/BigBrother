using System.Windows;
using frontend.ViewModels;

namespace frontend.Views;

public partial class WhitelistEditWindow : Window
{
    private readonly WhitelistEditViewModel _viewModel;

    public WhitelistEditWindow()
    {
        InitializeComponent();
        _viewModel = new WhitelistEditViewModel();
        _viewModel.Saved += OnSaved;
        DataContext = _viewModel;
    }

    private void OnSaved(object? sender, EventArgs e)
    {
        DialogResult = true;
        Close();
    }

    public WhitelistEditViewModel ViewModel => _viewModel;
}
