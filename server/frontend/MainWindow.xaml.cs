using System.Windows;
using frontend.ViewModels;

namespace frontend;

public partial class MainWindow : Window
{
    public MainWindow()
    {
        InitializeComponent();
        Loaded += (s, e) =>
        {
            if (DataContext is MainViewModel vm)
            {
                vm.AutoScrollRequested += () =>
                {
                    if (ServerLogListBox.Items.Count > 0)
                    {
                        ServerLogListBox.ScrollIntoView(ServerLogListBox.Items[^1]);
                    }
                };
            }
        };
    }

    protected override void OnClosed(EventArgs e)
    {
        if (DataContext is MainViewModel vm)
        {
            vm.Dispose();
        }
        base.OnClosed(e);
    }
}