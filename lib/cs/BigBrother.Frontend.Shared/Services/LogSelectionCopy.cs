using System.Runtime.InteropServices;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;

namespace frontend.Services;

public static class LogSelectionCopy
{
    public static void Enable<T>(ListBox listBox, Func<T, string> format, Action<string>? copyFailed = null)
    {
        listBox.SelectionMode = SelectionMode.Extended;
        listBox.ToolTip = "Shift/Ctrl + щелчок — выбор строк; Ctrl+A — выделить всё; Ctrl+C — копировать";
        listBox.CommandBindings.Add(new CommandBinding(ApplicationCommands.Copy, (_, e) =>
        {
            var selected = listBox.SelectedItems.Cast<T>().ToHashSet();
            if (selected.Count == 0) return;
            string text = string.Join(Environment.NewLine,
                listBox.Items.Cast<T>().Where(selected.Contains).Select(format));
            try
            {
                Clipboard.SetText(text);
            }
            catch (ExternalException)
            {
                const string message = "Не удалось скопировать записи. Повторите попытку.";
                if (copyFailed != null) copyFailed(message);
                else MessageBox.Show(Window.GetWindow(listBox), message, "Копирование логов",
                    MessageBoxButton.OK, MessageBoxImage.Warning);
            }
            e.Handled = true;
        }, (_, e) =>
        {
            e.CanExecute = listBox.SelectedItems.Count > 0;
            e.Handled = true;
        }));
        listBox.CommandBindings.Add(new CommandBinding(ApplicationCommands.SelectAll, (_, e) =>
        {
            listBox.SelectAll();
            e.Handled = true;
        }, (_, e) =>
        {
            e.CanExecute = listBox.Items.Count > 0;
            e.Handled = true;
        }));
        var menu = new ContextMenu { Style = (Style)listBox.FindResource("LogContextMenuStyle") };
        menu.Items.Add(new MenuItem
        {
            Header = "Копировать", Command = ApplicationCommands.Copy, CommandTarget = listBox
        });
        menu.Items.Add(new MenuItem
        {
            Header = "Выделить всё", Command = ApplicationCommands.SelectAll, CommandTarget = listBox
        });
        listBox.ContextMenu = menu;
    }
}
