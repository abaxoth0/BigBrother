using System.Collections.ObjectModel;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Data;
using System.Windows.Input;
using frontend.Services;
using ClientLogEntry = frontend.Models.LogEntry;
using ServerLogEntry = frontend.Models.ServerLogEntry;

internal static partial class Program
{
    private static void TestLiveLogSelection()
    {
        CheckLiveLogSelection(new[]
        {
            new ClientLogEntry { Timestamp = "12:01:00", Level = "INFO", Message = "[FW] first" },
            new ClientLogEntry { Timestamp = "12:01:01", Level = "BLOCKED", Message = "[FW] middle" },
            new ClientLogEntry { Timestamp = "12:01:02", Level = "ERROR", Message = "[CL] last" }
        }, new ClientLogEntry { Timestamp = "12:01:03", Level = "INFO", Message = "new entry" },
            LogEntryFormatter.Format,
            $"[12:01:00] [INFO] [FW] first{Environment.NewLine}[12:01:02] [ERROR] [CL] last",
            "[12:01:01] [BLOCKED] [FW] middle");
        CheckLiveLogSelection(new[]
        {
            new ServerLogEntry { Timestamp = "12:01:00", Source = "SERVER", Level = "INFO", Message = "first" },
            new ServerLogEntry { Timestamp = "12:01:01", Source = "DATABASE", Level = "DEBUG", Message = "middle" },
            new ServerLogEntry { Timestamp = "12:01:02", Source = "SERVER", Level = "ERROR", Message = "last" }
        }, new ServerLogEntry { Timestamp = "12:01:03", Source = "SERVER", Level = "INFO", Message = "new entry" },
            LogEntryFormatter.Format,
            $"[12:01:00] [SERVER] [INFO] first{Environment.NewLine}[12:01:02] [SERVER] [ERROR] last",
            "[12:01:01] [DATABASE] [DEBUG] middle");
    }

    private static void CheckLiveLogSelection<T>(T[] initial, T arriving, Func<T, string> format,
        string expectedSelected, string expectedFiltered) where T : class
    {
        var entries = new ObservableCollection<T>(initial);
        var view = new ListCollectionView(entries);
        var list = new ListBox { ItemsSource = view };
        LogSelectionCopy.Enable(list, format);
        Check(list.SelectionMode == SelectionMode.Extended, "Live logs must support multiple-row selection");
        list.UnselectAll();
        Check(!ApplicationCommands.Copy.CanExecute(null, list), "Copy must be disabled before selecting live logs");
        list.SelectedItems.Add(initial[2]);
        list.SelectedItems.Add(initial[0]);
        entries.Add(arriving);
        Check(list.SelectedItems.Count == 2, "Incoming logs must preserve the existing selection");
        ApplicationCommands.Copy.Execute(null, list);
        Check(Clipboard.GetText() == expectedSelected, "Live log copying must preserve display order and metadata");
        var selectAll = (MenuItem)list.ContextMenu.Items[1];
        ((RoutedCommand)selectAll.Command).Execute(selectAll.CommandParameter, selectAll.CommandTarget);
        Check(list.SelectedItems.Count == 4, "Select All must include every currently displayed live entry");
        view.Filter = entry => ReferenceEquals(entry, initial[1]);
        ApplicationCommands.SelectAll.Execute(null, list);
        var copy = (MenuItem)list.ContextMenu.Items[0];
        ((RoutedCommand)copy.Command).Execute(copy.CommandParameter, copy.CommandTarget);
        Check(Clipboard.GetText() == expectedFiltered, "Context-menu copying must respect the active log filter");
        entries.Clear();
        Check(!ApplicationCommands.Copy.CanExecute(null, list) && !ApplicationCommands.SelectAll.CanExecute(null, list),
            "Cleared live logs must disable copy and Select All");
    }
}
