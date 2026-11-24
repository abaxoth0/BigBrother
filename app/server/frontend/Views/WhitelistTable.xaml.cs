using System.Collections;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Controls.Primitives;
using System.Windows.Input;
using System.Windows.Media;
using frontend.Models;
using frontend.ViewModels;

namespace frontend.Views;

public partial class WhitelistTable : UserControl
{
    private readonly WhitelistSelection _whitelistSelection = new();
    public static readonly DependencyProperty ItemsSourceProperty = DependencyProperty.Register(
        nameof(ItemsSource), typeof(IEnumerable), typeof(WhitelistTable));
    public static readonly DependencyProperty IsTemporaryProperty = DependencyProperty.Register(
        nameof(IsTemporary), typeof(bool), typeof(WhitelistTable), new PropertyMetadata(false));
    public IEnumerable ItemsSource { get => (IEnumerable)GetValue(ItemsSourceProperty); set => SetValue(ItemsSourceProperty, value); }
    public bool IsTemporary { get => (bool)GetValue(IsTemporaryProperty); set => SetValue(IsTemporaryProperty, value); }

    public WhitelistTable()
    {
        InitializeComponent();
        Loaded += (_, _) =>
        {
            var visibility = IsTemporary ? Visibility.Visible : Visibility.Collapsed;
            DurationColumn.Visibility = RemainingColumn.Visibility = AutoDeleteColumn.Visibility = visibility;
            EditTimerItem.Visibility = ResetTimerItem.Visibility = visibility;
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
}
