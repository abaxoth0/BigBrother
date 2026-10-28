using System.Runtime.CompilerServices;
using System.Windows;
using System.Windows.Controls;
using frontend;
using frontend.ViewModels;

internal static partial class Program
{
    private sealed class ChromeProbe : ChromeWindow
    {
        public void ToggleMaximize() => MaximizeRestoreButton_Click(this, new RoutedEventArgs());
    }

    private static void TestAppearance()
    {
        var window = new ChromeProbe { Width = 800, Height = 500 };
        var panel = new StackPanel();
        var body = new TextBlock { Text = "Body" };
        var heading = new TextBlock { Text = "Heading" };
        heading.SetResourceReference(TextBlock.FontSizeProperty, "FontSize22");
        var editor = new TextBox { Text = "Editor" };
        editor.SetResourceReference(Control.FontSizeProperty, "FontSize15");
        var maximize = new Button { Style = (Style)window.FindResource("MaximizeRestoreButton") };
        panel.Children.Add(body);
        panel.Children.Add(heading);
        panel.Children.Add(editor);
        panel.Children.Add(maximize);
        window.Content = panel;
        try
        {
            window.Show();
            foreach (int size in new[] { 10, 14, 16, 20, 28 })
            {
                AppearanceSettings.Current.BaseFontSize = size;
                window.UpdateLayout();
                Check(Math.Abs(body.FontSize - size) < 0.001, "Body text must inherit the chosen base size");
                Check(Math.Abs(heading.FontSize / body.FontSize - 22.0 / 14) < 0.001,
                    "Heading proportions must survive live size changes");
                Check(Math.Abs(editor.FontSize / body.FontSize - 15.0 / 14) < 0.001,
                    "Editor text must scale with the body");
                Check(editor.DesiredSize.Height >= editor.FontSize, "Input controls must fit scaled text");
            }
            window.ToggleMaximize();
            window.UpdateLayout();
            Check(window.WindowState == WindowState.Maximized && (string)maximize.ToolTip == "Восстановить",
                "Maximize must update the restore action");
            window.ToggleMaximize();
            window.UpdateLayout();
            Check(window.WindowState == WindowState.Normal && (string)maximize.ToolTip == "Развернуть",
                "Restore must return to the original window state");
            window.ResizeMode = ResizeMode.NoResize;
            window.ToggleMaximize();
            Check(window.WindowState == WindowState.Normal, "Fixed dialogs must not maximize");
        }
        finally
        {
            window.Close();
            AppearanceSettings.Current.BaseFontSize = 14;
        }

        var vm = (MainViewModel)RuntimeHelpers.GetUninitializedObject(typeof(MainViewModel));
        vm.SelectedSection = NavigationSection.Whitelist;
        Check(vm.SelectedSection == NavigationSection.Main, "Offline navigation must stay on Main");
        var connection = typeof(MainViewModel).GetProperty(nameof(MainViewModel.BackendConnected))!;
        connection.GetSetMethod(nonPublic: true)!.Invoke(vm, new object[] { true });
        vm.SelectedSection = NavigationSection.Whitelist;
        Check(vm.SelectedSection == NavigationSection.Whitelist, "Backend connection must unlock navigation");
        connection.GetSetMethod(nonPublic: true)!.Invoke(vm, new object[] { false });
        Check(vm.SelectedSection == NavigationSection.Main, "Backend loss must return to Main");

        Console.WriteLine("PASS: live proportional typography, maximize/restore and backend-dependent navigation");
    }
}
