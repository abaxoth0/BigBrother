using System.IO;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using System.Windows.Shell;
using System.Windows.Threading;
using frontend;

internal static partial class Program
{
    private static IEnumerable<T> DialogChildren<T>(DependencyObject parent) where T : DependencyObject
    {
        for (int index = 0; index < VisualTreeHelper.GetChildrenCount(parent); index++)
        {
            var child = VisualTreeHelper.GetChild(parent, index);
            if (child is T value) yield return value;
            foreach (var descendant in DialogChildren<T>(child)) yield return descendant;
        }
    }

    private static void DuringDialog<T>(Action<T> action) where T : Window
    {
        Dispatcher.CurrentDispatcher.BeginInvoke(DispatcherPriority.ApplicationIdle, new Action(() =>
        {
            var window = Application.Current.Windows.OfType<T>().Single(w => w.IsVisible);
            try { action(window); }
            catch (Exception ex) { _dialogError = ex; }
            finally { if (window.IsVisible) window.Close(); }
        }));
    }

    private static Exception? _dialogError;

    private static void CheckDialogCallback()
    {
        var error = _dialogError;
        _dialogError = null;
        if (error != null) throw new Exception("Dialog UI check failed", error);
    }

    private static void TestDialogs()
    {
        int textSize = AppearanceSettings.Current.TextFontSize;
        int interfaceSize = AppearanceSettings.Current.InterfaceFontSize;
        var owner = new DialogWindow { Title = "Владелец", Content = new TextBlock { Text = "Owner" } };
        owner.Show();
        try
        {
            foreach (var (buttons, captions, results) in new[]
            {
                (MessageBoxButton.OK, new[] { "ОК" }, new[] { MessageBoxResult.OK }),
                (MessageBoxButton.OKCancel, new[] { "ОК", "Отмена" }, new[] { MessageBoxResult.OK, MessageBoxResult.Cancel }),
                (MessageBoxButton.YesNo, new[] { "Да", "Нет" }, new[] { MessageBoxResult.Yes, MessageBoxResult.No }),
                (MessageBoxButton.YesNoCancel, new[] { "Да", "Нет", "Отмена" }, new[] { MessageBoxResult.Yes, MessageBoxResult.No, MessageBoxResult.Cancel })
            })
            {
                for (int index = 0; index < captions.Length; index++)
                {
                    string caption = captions[index];
                    DuringDialog<MessageDialog>(dialog =>
                    {
                        dialog.UpdateLayout();
                        Check(dialog.Owner == owner && dialog.WindowStyle == WindowStyle.None && !dialog.ShowInTaskbar,
                            "Messages must use custom chrome and belong to their invoking window");
                        CheckWindowFrame(dialog);
                        var actions = DialogChildren<WrapPanel>(dialog).Single().Children.OfType<Button>().ToArray();
                        Check(actions.Select(button => (string)button.Content).SequenceEqual(captions), "Dialog actions changed");
                        Check(actions[0].IsDefault && actions[0].IsKeyboardFocused, "Enter must initially activate the default action");
                        actions.Single(button => Equals(button.Content, caption)).RaiseEvent(new RoutedEventArgs(Button.ClickEvent));
                    });
                    var result = MessageDialog.Show(owner, "Сообщение с токеном: abc-123", "Проверка", buttons, MessageBoxImage.Information);
                    CheckDialogCallback();
                    Check(result == results[index], "Dialog action returned an incorrect result");
                }
            }
            DuringDialog<MessageDialog>(dialog =>
            {
                var focused = DialogChildren<Button>(dialog).Single(button => button.IsDefault);
                Check(Equals(focused.Content, "Нет") && focused.IsKeyboardFocused, "Explicit No default must be preserved");
                focused.RaiseEvent(new RoutedEventArgs(Button.ClickEvent));
            });
            Check(MessageDialog.Show(owner, "Удалить пресет?", "Удаление", MessageBoxButton.YesNo,
                MessageBoxImage.Question, MessageBoxResult.No) == MessageBoxResult.No, "No must decline deletion");
            CheckDialogCallback();

            foreach (var (buttons, expected) in new[]
            {
                (MessageBoxButton.OK, MessageBoxResult.OK), (MessageBoxButton.OKCancel, MessageBoxResult.Cancel),
                (MessageBoxButton.YesNo, MessageBoxResult.No), (MessageBoxButton.YesNoCancel, MessageBoxResult.Cancel)
            })
            {
                DuringDialog<MessageDialog>(dialog => dialog.RaiseEvent(new KeyEventArgs(Keyboard.PrimaryDevice,
                    PresentationSource.FromVisual(dialog), Environment.TickCount, Key.Escape) { RoutedEvent = Keyboard.PreviewKeyDownEvent }));
                Check(MessageDialog.Show(owner, "Отмена", "Проверка", buttons) == expected, "Escape must not confirm an action");
                CheckDialogCallback();
                DuringDialog<MessageDialog>(dialog => DialogChildren<Button>(dialog).Single(button => Equals(button.Tag, "Close"))
                    .RaiseEvent(new RoutedEventArgs(Button.ClickEvent)));
                Check(MessageDialog.Show(owner, "Закрыть", "Проверка", buttons) == expected, "Title-bar close must not confirm an action");
                CheckDialogCallback();
            }

            owner.Activate();
            AppearanceSettings.Current.TextFontSize = 28;
            AppearanceSettings.Current.InterfaceFontSize = 28;
            string longText = string.Join("\n", Enumerable.Repeat("Длинное сообщение с токеном доступа abc-123", 150));
            DuringDialog<MessageDialog>(dialog =>
            {
                dialog.UpdateLayout();
                Check(dialog.Owner == owner, "Messages without an explicit owner must use the active window");
                var text = DialogChildren<TextBox>(dialog).Single();
                Check(text.IsReadOnly && text.Text == longText && text.FontSize == 28, "Messages must retain selectable, scaled text");
                var scroll = DialogChildren<ScrollViewer>(text).Single();
                Check(scroll.ScrollableHeight > 0, "Long messages must scroll");
                var action = DialogChildren<Button>(dialog).Single(button => button.IsDefault);
                var position = action.TransformToAncestor(dialog).Transform(new Point());
                Check(position.Y + action.ActualHeight <= dialog.ActualHeight && action.FontSize == 28,
                    "Actions must remain visible at maximum font sizes");
                var icon = DialogChildren<Border>(dialog).Single(border => border.CornerRadius.TopLeft == 100);
                var symbol = (TextBlock)icon.Child;
                Check(icon.ActualHeight >= symbol.ActualHeight && icon.ActualWidth >= symbol.ActualWidth,
                    "Status symbols must fit inside their icon at maximum interface size");
                Check(WindowChrome.GetWindowChrome(dialog).CaptionHeight >= 40, "Custom title bar must retain its drag area");
                if (Environment.GetEnvironmentVariable("BIGBROTHER_DIALOG_SCREENSHOT") is { Length: > 0 } path)
                {
                    var encoder = new PngBitmapEncoder();
                    encoder.Frames.Add(BitmapFrame.Create(RenderWindowFrame(dialog)));
                    using var file = File.Create(path);
                    encoder.Save(file);
                }
                text.Select(0, 10);
                text.Copy();
                Check(Clipboard.GetText() == longText[..10], "Message contents must support copying tokens and errors");
            });
            MessageDialog.Show(longText, "Длинное сообщение", MessageBoxButton.OK, MessageBoxImage.Warning);
            CheckDialogCallback();

            var credentials = new NetworkCredentialWindow(@"\\server\updates") { Owner = owner };
            PasswordBox? password = null;
            DuringDialog<NetworkCredentialWindow>(dialog =>
            {
                dialog.UpdateLayout();
                CheckWindowFrame(dialog);
                var user = DialogChildren<TextBox>(dialog).Single();
                password = DialogChildren<PasswordBox>(dialog).Single();
                Check(user.IsKeyboardFocused && password.FontSize == 28 && password.Background is SolidColorBrush,
                    "Credentials must focus the username and use themed, scaled password inputs");
                var connect = DialogChildren<Button>(dialog).Single(button => Equals(button.Content, "Подключиться"));
                connect.RaiseEvent(new RoutedEventArgs(Button.ClickEvent));
                Check(DialogChildren<TextBlock>(dialog).Any(block => block.Text == "Введите имя пользователя."),
                    "Empty usernames must be validated before connecting");
                password.Password = "test secret";
                DialogChildren<Button>(dialog).Single(button => Equals(button.Tag, "Close"))
                    .RaiseEvent(new RoutedEventArgs(Button.ClickEvent));
            });
            Check(credentials.ShowDialog() != true, "Closing credentials must cancel authentication");
            CheckDialogCallback();
            Check(password?.Password == "", "Credentials must clear passwords when closed");
            Console.WriteLine("PASS: themed dialogs, owners, actions/default focus, cancellation, copy, long-message scrolling and credential validation");
        }
        finally
        {
            owner.Close();
            AppearanceSettings.Current.TextFontSize = textSize;
            AppearanceSettings.Current.InterfaceFontSize = interfaceSize;
        }
    }
}
