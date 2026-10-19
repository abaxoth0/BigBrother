using System.Buffers.Binary;
using System.IO;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
using System.Windows.Media;
using frontend;
using frontend.Models;
using frontend.ViewModels;

internal static partial class Program
{
    private static void TestHistorySelection()
    {
        var app = new Application { ShutdownMode = ShutdownMode.OnExplicitShutdown };
        foreach (string theme in new[] { "Theme.xaml", "ThemeDark.xaml" })
            app.Resources.MergedDictionaries.Add(new ResourceDictionary
            {
                Source = new Uri("pack://application:,,,/BigBrother.Frontend.Shared;component/" + theme)
            });
        string directory = Path.Combine(Path.GetTempPath(), "bigbrother-history-selection-" + Guid.NewGuid());
        Directory.CreateDirectory(directory);
        LogViewWindow? window = null;
        try
        {
            var timestamp = new DateTimeOffset(2026, 10, 20, 13, 14, 15, TimeSpan.Zero);
            string path = Path.Combine(directory, "history.binlog");
            using (var output = File.Create(path))
            {
                foreach (string message in new[] { "first entry", "middle entry", "last entry" })
                {
                    var record = Record(message);
                    BinaryPrimitives.WriteInt64LittleEndian(record.AsSpan(4, 8), timestamp.ToUnixTimeSeconds());
                    output.Write(record);
                }
            }
            window = new LogViewWindow(new List<LogFileSource>
            {
                new() { Label = "Firewall", FilePath = path },
                new() { Label = "Client", FilePath = path }
            });
            var tabs = (TabControl)window.FindName("LogTabs");
            if (!Fonts.SystemFontFamilies.Any(font => font.Source == "Consolas"))
                foreach (TabItem tab in tabs.Items)
                    ((ListBox)tab.Content).FontFamily = new FontFamily("Liberation Mono");
            window.Show();
            var history = (LogViewViewModel)window.DataContext;
            PumpUntil(() => history.Sources.All(source => source.AllEntries.Count == 3), "History window did not load", busyRendering: false);
            window.UpdateLayout();
            var list = (ListBox)((TabItem)tabs.Items[0]).Content;
            string time = timestamp.LocalDateTime.ToString("yyyy-MM-dd HH:mm:ss");
            var item = (ListBoxItem)list.ItemContainerGenerator.ContainerFromIndex(0);
            Check(FindText(item)?.Text == $"[{time}] first entry", "Each displayed history row must include its local timestamp");
            Check(list.SelectionMode == SelectionMode.Extended && VirtualizingPanel.GetIsVirtualizing(list),
                "History selection must support multiple rows while retaining virtualization");
            list.UnselectAll();
            Check(!ApplicationCommands.Copy.CanExecute(null, list), "Copy must be disabled without selected records");
            list.SelectedItems.Add(list.Items[2]);
            list.SelectedItems.Add(list.Items[0]);
            ApplicationCommands.Copy.Execute(null, list);
            Check(Clipboard.GetText() == $"[{time}] [Firewall] [Info] first entry{Environment.NewLine}[{time}] [Firewall] [Info] last entry",
                "Copy must preserve file order and include timestamps, source and level");
            ApplicationCommands.SelectAll.Execute(null, list);
            Check(list.SelectedItems.Count == 3, "Select All must select every displayed entry");
            history.SearchText = "middle";
            Check(list.Items.Count == 1, "Search must update the displayed history");
            ApplicationCommands.SelectAll.Execute(null, list);
            var copyMenu = (MenuItem)list.ContextMenu.Items[0];
            ((RoutedCommand)copyMenu.Command).Execute(copyMenu.CommandParameter, copyMenu.CommandTarget);
            Check(Clipboard.GetText() == $"[{time}] [Firewall] [Info] middle entry", "Context-menu copy must include only selected filtered records");
            tabs.SelectedIndex = 1;
            window.UpdateLayout();
            var client = (ListBox)((TabItem)tabs.Items[1]).Content;
            ApplicationCommands.SelectAll.Execute(null, client);
            ApplicationCommands.Copy.Execute(null, client);
            Check(Clipboard.GetText() == $"[{time}] [Client] [Info] middle entry", "Copy must use the selected tab's source");
            var search = (TextBox)window.FindName("SearchTextBox");
            search.SelectAll();
            ApplicationCommands.Copy.Execute(null, search);
            Check(Clipboard.GetText() == "middle", "Search-box text copying must remain available");
            TestLiveLogSelection();
        }
        finally
        {
            window?.Close();
            Directory.Delete(directory, true);
            app.Shutdown();
        }
    }

    private static TextBlock? FindText(DependencyObject parent)
    {
        for (int i = 0; i < VisualTreeHelper.GetChildrenCount(parent); i++)
        {
            var child = VisualTreeHelper.GetChild(parent, i);
            if (child is TextBlock text) return text;
            if (FindText(child) is { } descendant) return descendant;
        }
        return null;
    }
}
