using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
using System.Windows.Media;
using System.Windows.Threading;

namespace frontend;

public sealed class MessageDialog : DialogWindow
{
    private MessageBoxResult _result;

    private MessageDialog(string message, string title, MessageBoxButton buttons, MessageBoxImage image,
        MessageBoxResult defaultResult)
    {
        Title = title;
        MaxHeight = Math.Min(MaxHeight, 720);
        _result = buttons == MessageBoxButton.YesNo ? MessageBoxResult.No :
            buttons == MessageBoxButton.OK ? MessageBoxResult.OK : MessageBoxResult.Cancel;
        var choices = buttons switch
        {
            MessageBoxButton.OK => new[] { MessageBoxResult.OK },
            MessageBoxButton.OKCancel => new[] { MessageBoxResult.OK, MessageBoxResult.Cancel },
            MessageBoxButton.YesNo => new[] { MessageBoxResult.Yes, MessageBoxResult.No },
            MessageBoxButton.YesNoCancel => new[] { MessageBoxResult.Yes, MessageBoxResult.No, MessageBoxResult.Cancel },
            _ => throw new ArgumentOutOfRangeException(nameof(buttons))
        };
        if (defaultResult == MessageBoxResult.None) defaultResult = choices[0];
        if (!choices.Contains(defaultResult)) throw new ArgumentException("Default result must match a button.", nameof(defaultResult));
        var grid = new Grid { Margin = new Thickness(20) };
        grid.RowDefinitions.Add(new RowDefinition { Height = new GridLength(1, GridUnitType.Star) });
        grid.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });
        var body = new Grid();
        body.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto });
        body.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(1, GridUnitType.Star) });
        if (image != MessageBoxImage.None)
        {
            var symbol = new TextBlock
            {
                Text = image switch { MessageBoxImage.Error => "×", MessageBoxImage.Warning => "!", MessageBoxImage.Question => "?", _ => "i" },
                FontWeight = FontWeights.Bold, HorizontalAlignment = HorizontalAlignment.Center,
                VerticalAlignment = VerticalAlignment.Center
            };
            symbol.SetResourceReference(TextBlock.FontSizeProperty, "FontSize22");
            var icon = new Border
            {
                Child = symbol, CornerRadius = new CornerRadius(100), BorderThickness = new Thickness(2),
                Margin = new Thickness(0, 0, 16, 0), VerticalAlignment = VerticalAlignment.Top
            };
            icon.SetResourceReference(WidthProperty, "Scaled40");
            icon.SetResourceReference(HeightProperty, "Scaled40");
            string colour = image switch { MessageBoxImage.Error => "StatusDisconnected", MessageBoxImage.Warning => "StatusWarning", _ => "AccentText" };
            icon.SetResourceReference(Border.BorderBrushProperty, colour);
            symbol.SetResourceReference(TextBlock.ForegroundProperty, colour);
            body.Children.Add(icon);
        }
        var text = new TextBox
        {
            Text = message, IsReadOnly = true, TextWrapping = TextWrapping.Wrap,
            Background = Brushes.Transparent, BorderThickness = new Thickness(0), Padding = new Thickness(0),
            VerticalContentAlignment = VerticalAlignment.Top,
            VerticalScrollBarVisibility = ScrollBarVisibility.Auto,
            HorizontalScrollBarVisibility = ScrollBarVisibility.Disabled
        };
        Grid.SetColumn(text, 1);
        body.Children.Add(text);
        grid.Children.Add(body);
        var actions = new WrapPanel { HorizontalAlignment = HorizontalAlignment.Right, Margin = new Thickness(0, 20, 0, 0) };
        Button? focused = null;
        foreach (var choice in choices)
        {
            var button = new Button
            {
                Content = choice switch { MessageBoxResult.Yes => "Да", MessageBoxResult.No => "Нет", MessageBoxResult.Cancel => "Отмена", _ => "ОК" },
                MinWidth = 90, Margin = new Thickness(8, 4, 0, 0), IsDefault = choice == defaultResult
            };
            if (button.IsDefault)
            {
                focused = button;
                button.SetResourceReference(StyleProperty, "PrimaryButton");
            }
            button.Click += (_, _) => { _result = choice; DialogResult = true; };
            actions.Children.Add(button);
        }
        Grid.SetRow(actions, 1);
        grid.Children.Add(actions);
        DialogContent = grid;
        Loaded += (_, _) => focused?.Focus();
        PreviewKeyDown += (_, e) =>
        {
            if (e.Key != Key.Escape) return;
            e.Handled = true;
            Close();
        };
    }

    public static MessageBoxResult Show(string message, string title, MessageBoxButton buttons = MessageBoxButton.OK,
        MessageBoxImage image = MessageBoxImage.None, MessageBoxResult defaultResult = MessageBoxResult.None) =>
        Show(null, message, title, buttons, image, defaultResult);

    public static MessageBoxResult Show(Window? owner, string message, string title, MessageBoxButton buttons = MessageBoxButton.OK,
        MessageBoxImage image = MessageBoxImage.None, MessageBoxResult defaultResult = MessageBoxResult.None)
    {
        var dispatcher = owner?.Dispatcher ?? Application.Current?.Dispatcher ?? Dispatcher.CurrentDispatcher;
        if (!dispatcher.CheckAccess())
            return dispatcher.Invoke(() => Show(owner, message, title, buttons, image, defaultResult));
        var dialog = new MessageDialog(message, title, buttons, image, defaultResult) { Owner = owner ?? FindOwner() };
        if (dialog.Owner == null) dialog.WindowStartupLocation = WindowStartupLocation.CenterScreen;
        dialog.ShowDialog();
        return dialog._result;
    }
}
