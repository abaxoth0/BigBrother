using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
using frontend.Models;
using frontend.Services;
using frontend.ViewModels;

namespace frontend;

public partial class MainWindow : ChromeWindow
{
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

    protected override void OnClosed(EventArgs e)
    {
        if (DataContext is MainViewModel vm)
        {
            vm.Dispose();
        }
        base.OnClosed(e);
    }
}
