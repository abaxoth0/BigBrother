using System.Windows;
using System.Windows.Controls;
using frontend.ViewModels;

namespace frontend;

public partial class MainWindow : Window
{
    public MainWindow()
    {
        InitializeComponent();
    }

    protected override void OnClosed(EventArgs e)
    {
        if (DataContext is MainViewModel vm)
        {
            vm.Dispose();
        }
        base.OnClosed(e);
    }

    private void SelectAllCheckBox_Click(object sender, RoutedEventArgs e)
    {
        if (DataContext is MainViewModel vm && sender is CheckBox checkBox)
        {
            bool isChecked = checkBox.IsChecked == true;
            foreach (var wl in vm.Whitelists)
            {
                wl.IsSelected = isChecked;
            }
        }
    }
}