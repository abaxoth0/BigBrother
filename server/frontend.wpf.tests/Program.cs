using System.Windows;
using System.Windows.Controls;
using System.Windows.Controls.Primitives;
using System.Windows.Media;
using System.Windows.Threading;
using frontend;
using frontend.Views;

internal static partial class Program
{
    [STAThread]
    private static void Main()
    {
        try { Run(); }
        catch (Exception ex)
        {
            Console.Error.WriteLine(ex);
            Environment.ExitCode = 1;
        }
    }

    private static void Run()
    {
        var app = new Application { ShutdownMode = ShutdownMode.OnExplicitShutdown };
        foreach (string theme in new[] { "Theme.xaml", "ThemeDark.xaml", "ServerAccent.xaml" })
            app.Resources.MergedDictionaries.Add(new ResourceDictionary
            {
                Source = new Uri("pack://application:,,,/BigBrother.Frontend.Shared;component/" + theme)
            });
        var editor = new WhitelistEditWindow();
        var input = (TextBox)editor.FindName("EntriesEditor");
        if (!Fonts.SystemFontFamilies.Any(font => font.Source == "Consolas"))
            input.FontFamily = new FontFamily("Liberation Mono");
        editor.ViewModel.EntriesText = "example.com\n# comment\nexample.org:443";
        var table = new DataGrid
        {
            AutoGenerateColumns = false,
            CanUserAddRows = false,
            ColumnHeaderStyle = (Style)app.FindResource("GridColumnHeaderStyle"),
            ItemsSource = new[] { new { Name = "example.com" } }
        };
        table.Columns.Add(new DataGridTextColumn
        {
            Header = "Rule", Binding = new System.Windows.Data.Binding("Name"), Width = 200
        });
        table.SetResourceReference(DataGrid.MinRowHeightProperty, "TableRowMinHeight");
        var tableWindow = new Window { Content = table, Width = 400, Height = 250 };
        try
        {
            editor.Show();
            tableWindow.Show();
            foreach (var (text, ui) in new[] { (14, 14), (28, 10), (10, 28), (20, 16) })
            {
                AppearanceSettings.Current.TextFontSize = text;
                AppearanceSettings.Current.InterfaceFontSize = ui;
                editor.UpdateLayout();
                tableWindow.UpdateLayout();
                Dispatcher.CurrentDispatcher.Invoke(() => { }, DispatcherPriority.ApplicationIdle);
                double entrySize = text * 15.0 / 14;
                Check(Math.Abs(input.FontSize - entrySize) < 0.001,
                    "Whitelist entries must follow text size independently of interface size");
                Check(editor.FontSize == ui, "Whitelist window labels must follow interface size");
                var gutter = (Canvas)editor.FindName("LineNumberGutter");
                Check(gutter.Children.OfType<TextBlock>().Any() &&
                      gutter.Children.OfType<TextBlock>().All(line => Math.Abs(line.FontSize - entrySize) < 0.001),
                    "Whitelist line numbers must stay aligned with scaled entries");
                var save = FindVisual<Button>(editor, button => Equals(button.Content, "Сохранить"));
                Check(save?.FontSize == ui, "Whitelist buttons must follow interface size");
                var row = (DataGridRow)table.ItemContainerGenerator.ContainerFromIndex(0);
                var cell = FindVisual<DataGridCell>(row, _ => true);
                var header = FindVisual<DataGridColumnHeader>(table, column => Equals(column.Content, "Rule"));
                Check(cell?.FontSize == text && header?.FontSize == ui,
                    "Table content and headers must use their respective font groups");
                Check(Math.Abs(table.MinRowHeight - text * 32.0 / 14) < 0.001 &&
                      header != null && Math.Abs(header.MinHeight - ui * 32.0 / 14) < 0.001,
                    "Table content and header heights must scale independently");
            }
            TestWindowFrames();
            TestDialogs();
            var networkShare = new NetworkShareWindow("");
            networkShare.Show();
            try { CheckWindowFrame(networkShare); }
            finally { networkShare.Close(); }
            TestSettingsNavigation(server: true);
            TestWhitelistToolbar();
            TestNetworkScheme();
            TestOfflineNavigation();
            Console.WriteLine("PASS: server whitelist editor startup, live text/interface scaling, line numbers and table styles");
        }
        finally
        {
            editor.Close();
            tableWindow.Close();
            app.Shutdown();
        }
    }

    private static T? FindVisual<T>(DependencyObject parent, Func<T, bool> match) where T : DependencyObject
    {
        for (int index = 0; index < VisualTreeHelper.GetChildrenCount(parent); index++)
        {
            var child = VisualTreeHelper.GetChild(parent, index);
            if (child is T value && match(value)) return value;
            if (FindVisual(child, match) is { } descendant) return descendant;
        }
        return null;
    }

    private static void Check(bool condition, string message)
    {
        if (!condition) throw new Exception(message);
    }
}
