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
    private readonly WhitelistSelection _whitelistSelection = new();
    private readonly CheckboxSelection<ConnectedClient> _connectedSelection = new();
    private readonly CheckboxSelection<PendingRegistration> _pendingSelection = new();
    private readonly CheckboxSelection<UserOverview> _allUsersSelection = new();

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

    private void WhitelistRow_PreviewMouseLeftButtonDown(object sender, MouseButtonEventArgs e)
    {
        if (sender is not DataGridRow row || row.Item is not WhitelistInfo whitelist ||
            ItemsControl.ItemsControlFromItemContainer(row) is not DataGrid grid) return;

        var control = Keyboard.Modifiers.HasFlag(ModifierKeys.Control);
        var shift = Keyboard.Modifiers.HasFlag(ModifierKeys.Shift);
        var items = grid.Items.OfType<WhitelistInfo>().ToList();
        if (!_whitelistSelection.HandleClick(items, whitelist, control, shift)) return;

        grid.SelectedItem = whitelist;
        row.Focus();
        e.Handled = true;
    }

    private void UserRow_PreviewMouseLeftButtonDown(object sender, MouseButtonEventArgs e)
    {
        if (sender is not DataGridRow row || ItemsControl.ItemsControlFromItemContainer(row) is not DataGrid grid) return;
        var control = Keyboard.Modifiers.HasFlag(ModifierKeys.Control);
        var shift = Keyboard.Modifiers.HasFlag(ModifierKeys.Shift);
        var handled = row.Item switch
        {
            ConnectedClient client => _connectedSelection.HandleClick(grid.Items.OfType<ConnectedClient>().ToList(), client, control, shift),
            PendingRegistration pending => _pendingSelection.HandleClick(grid.Items.OfType<PendingRegistration>().ToList(), pending, control, shift),
            UserOverview user => _allUsersSelection.HandleClick(grid.Items.OfType<UserOverview>().ToList(), user, control, shift),
            _ => false
        };
        if (!handled) return;
        grid.SelectedItem = row.Item;
        row.Focus();
        e.Handled = true;
    }

    private void WhitelistTable_PreviewKeyDown(object sender, KeyEventArgs e)
    {
        if (e.Key != Key.Enter || Keyboard.Modifiers != ModifierKeys.None ||
            sender is not DataGrid grid || grid.SelectedItem is not WhitelistInfo whitelist ||
            DataContext is not MainViewModel vm || !vm.OpenEditWhitelistCommand.CanExecute(whitelist)) return;

        e.Handled = true;
        vm.OpenEditWhitelistCommand.Execute(whitelist);
    }

    private void WhitelistRow_MouseDoubleClick(object sender, MouseButtonEventArgs e)
    {
        if (e.ChangedButton != MouseButton.Left ||
            (Keyboard.Modifiers & (ModifierKeys.Control | ModifierKeys.Shift)) != 0 || sender is not DataGridRow row ||
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
