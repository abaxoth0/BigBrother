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
        var appearance = new AppearanceEditor();
        Check(AppearanceSettings.Current.TextFontSize == 14 && AppearanceSettings.Current.InterfaceFontSize == 14,
            "Loading the appearance control must preserve both defaults");
        var body = new TextBlock { Text = "Label" };
        var button = new Button { Content = "Action" };
        var label = new Label { Content = "Field label" };
        var heading = new TextBlock { Text = "Heading" };
        heading.SetResourceReference(TextBlock.FontSizeProperty, "FontSize22");
        var editor = new TextBox { Text = "Editor" };
        editor.SetResourceReference(Control.FontSizeProperty, "TextFontSize15");
        var field = new TextBox { Text = "Editable value" };
        var logs = new ListBox { ItemContainerStyle = (Style)window.FindResource("LogRowStyle") };
        logs.SetResourceReference(Control.FontSizeProperty, "TextFontSize12");
        logs.Items.Add("Log entry");
        var maximize = new Button { Style = (Style)window.FindResource("MaximizeRestoreButton") };
        foreach (var child in new UIElement[] { body, button, label, heading, editor, field, logs, maximize, appearance })
            panel.Children.Add(child);
        window.Content = panel;
        try
        {
            window.Show();
            foreach (int size in new[] { 10, 14, 16, 20, 28 })
            {
                AppearanceSettings.Current.TextFontSize = size;
                window.UpdateLayout();
                Check(body.FontSize == 14 && button.FontSize == 14 && label.FontSize == 14 && heading.FontSize == 22,
                    "Text size changes must preserve interface labels, buttons and headings");
                Check(field.FontSize == size, "Editable fields must follow text size");
                Check(Math.Abs(editor.FontSize - size * 15.0 / 14) < 0.001,
                    "Editor proportions must follow text size");
                Check(Math.Abs(logs.FontSize - size * 12.0 / 14) < 0.001,
                    "Logs must follow text size");
                var row = (ListBoxItem)logs.ItemContainerGenerator.ContainerFromIndex(0);
                Check(Math.Abs(row.MinHeight - size * 22.0 / 14) < 0.001 &&
                      Math.Abs(row.Padding.Top - size * 2.0 / 14) < 0.001,
                    "Compact log row heights and padding must follow text size");
                Check(editor.DesiredSize.Height >= editor.FontSize, "Inputs must fit larger text");
            }
            foreach (int size in new[] { 10, 14, 16, 20, 28 })
            {
                AppearanceSettings.Current.InterfaceFontSize = size;
                window.UpdateLayout();
                Check(body.FontSize == size && button.FontSize == size && label.FontSize == size,
                    "Interface labels and buttons must follow interface size");
                Check(Math.Abs(heading.FontSize / body.FontSize - 22.0 / 14) < 0.001,
                    "Interface heading proportions must survive size changes");
                Check(field.FontSize == 28 && editor.FontSize == 30 && logs.FontSize == 24,
                    "Interface size changes must preserve text in inputs, editors and logs");
                Check(Math.Abs((double)window.FindResource("TableHeaderMinHeight") - size * 32.0 / 14) < 0.001 &&
                      (double)window.FindResource("TableRowMinHeight") == 64,
                    "Table headers and content rows must follow their respective sizes");
                Check(((TextBox)appearance.FindName("TextFontSizeInput")).FontSize == size,
                    "Appearance controls must remain part of the interface");
            }
            AppearanceSettings.Current.TextFontSize = 14;
            AppearanceSettings.Current.InterfaceFontSize = 14;
            TestFontInput(appearance, "Text");
            TestFontInput(appearance, "Interface");
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
            AppearanceSettings.Current.TextFontSize = 14;
            AppearanceSettings.Current.InterfaceFontSize = 14;
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

        Console.WriteLine("PASS: independent text/interface typography, numeric inputs, row scaling, maximize/restore and navigation");
    }

    private static void TestFontInput(AppearanceEditor appearance, string kind)
    {
        var settings = AppearanceSettings.Current;
        bool text = kind == "Text";
        int Size() => text ? settings.TextFontSize : settings.InterfaceFontSize;
        int OtherSize() => text ? settings.InterfaceFontSize : settings.TextFontSize;
        int other = OtherSize();
        var input = (TextBox)appearance.FindName(kind + "FontSizeInput");
        var increase = (Button)appearance.FindName("Increase" + kind + "FontSizeButton");
        var decrease = (Button)appearance.FindName("Decrease" + kind + "FontSizeButton");
        input.SetCurrentValue(TextBox.TextProperty, "16");
        input.RaiseEvent(new RoutedEventArgs(UIElement.LostFocusEvent));
        Check(Size() == 16 && OtherSize() == other, "Typed font sizes must update only their own group");
        increase.RaiseEvent(new RoutedEventArgs(Button.ClickEvent));
        Check(Size() == 17 && input.Text == "17", "Plus must increase its font size by one");
        decrease.RaiseEvent(new RoutedEventArgs(Button.ClickEvent));
        Check(Size() == 16 && input.Text == "16", "Minus must decrease its font size by one");
        foreach (var (value, expected) in new[] { ("invalid", 16), ("99", 28), ("", 28), ("1", 10) })
        {
            input.SetCurrentValue(TextBox.TextProperty, value);
            input.RaiseEvent(new RoutedEventArgs(UIElement.LostFocusEvent));
            Check(Size() == expected && input.Text == expected.ToString() && OtherSize() == other,
                "Invalid input and clamping must preserve the other font size");
            Check(decrease.IsEnabled == (expected > 10) && increase.IsEnabled == (expected < 28),
                "Each pair of buttons must follow its own bounds");
        }
    }

}
