using System.Windows;
using System.Windows.Controls;
using frontend.Services;

namespace frontend.Views;

public sealed class ProcessBlacklistWindow : DialogWindow
{
    public ProcessBlacklistWindow(IpcService ipc, List<string> source)
    {
        Title = "Чёрный список приложений";
        Width = 620;

        var panel = new StackPanel { Margin = new Thickness(16) };
        panel.Children.Add(new TextBlock
        {
            Text =
                "По одному имени .exe в строке. Например, game*.exe завершает подходящие процессы, " +
                "а !game-editor.exe разрешает редактор. " +
                "Выигрывает правило с большим приоритетом [N], при равенстве — исключение. " +
                "Пустой список отключает блокировку.",
            TextWrapping = TextWrapping.Wrap,
            Margin = new Thickness(0, 0, 0, 8)
        });

        var editor = new TextBox
        {
            AcceptsReturn = true,
            MinLines = 5,
            Height = Math.Min(280, MaxHeight / 2),
            HorizontalScrollBarVisibility = ScrollBarVisibility.Auto,
            VerticalScrollBarVisibility = ScrollBarVisibility.Auto,
            Text = string.Join(Environment.NewLine, source)
        };
        editor.SetResourceReference(FontSizeProperty, "TextFontSize14");
        panel.Children.Add(editor);

        var status = new TextBlock
        {
            TextWrapping = TextWrapping.Wrap,
            Margin = new Thickness(0, 6, 0, 0)
        };
        panel.Children.Add(status);

        var buttons = new StackPanel
        {
            Orientation = Orientation.Horizontal,
            HorizontalAlignment = HorizontalAlignment.Right
        };
        var cancel = new Button { Content = "Отмена", IsCancel = true, Margin = new Thickness(4) };
        var save = new Button { Content = "Сохранить", Margin = new Thickness(4) };
        save.SetResourceReference(StyleProperty, "PrimaryButton");
        buttons.Children.Add(cancel);
        buttons.Children.Add(save);
        panel.Children.Add(buttons);
        DialogContent = panel;

        bool saving = false;
        Closing += (_, e) => e.Cancel = saving;
        save.Click += async (_, _) =>
        {
            saving = true;
            panel.IsEnabled = false;
            try
            {
                await ipc.SaveProcessBlacklistAsync(editor.Text.ReplaceLineEndings("\n").Split('\n'));
            }
            catch (Exception ex)
            {
                status.Text = ex.Message;
                return;
            }
            finally
            {
                saving = false;
                panel.IsEnabled = true;
            }

            DialogResult = true;
        };
        Loaded += (_, _) => editor.Focus();
    }
}
