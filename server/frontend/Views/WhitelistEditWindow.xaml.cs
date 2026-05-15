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
        DataContext = _viewModel;
    }

    public WhitelistEditViewModel ViewModel => _viewModel;
}
