using System.Windows;
using System.Windows.Controls;
using System.Windows.Controls.Primitives;
using System.Windows.Input;
using System.Windows.Media;
using frontend.Models;
using frontend.Services;
using frontend.ViewModels;

namespace frontend;

public partial class MainWindow : ChromeWindow
{
    public MainWindow()
    {
        InitializeComponent();
        LogSelectionCopy.Enable<ServerLogEntry>(ServerLogListBox, LogEntryFormatter.Format);
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

    private void WhitelistRow_MouseDoubleClick(object sender, MouseButtonEventArgs e)
    {
        if (e.ChangedButton != MouseButton.Left || sender is not DataGridRow row ||
            row.Item is not WhitelistInfo whitelist || DataContext is not MainViewModel vm) return;

        // Selection checkboxes keep their own click behavior.
        for (var element = e.OriginalSource as DependencyObject; element != null && element != row;
             element = element is FrameworkContentElement content ? content.Parent : VisualTreeHelper.GetParent(element))
        {
            if (element is ButtonBase) return;
        }

        if (vm.OpenEditWhitelistCommand.CanExecute(whitelist))
        {
            e.Handled = true;
            vm.OpenEditWhitelistCommand.Execute(whitelist);
        }
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
