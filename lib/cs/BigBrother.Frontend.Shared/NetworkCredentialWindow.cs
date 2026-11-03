using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
using frontend.Services;

namespace frontend;

public sealed class NetworkCredentialWindow : DialogWindow
{
    private readonly string _share;
    private readonly TextBox _user = new();
    private readonly PasswordBox _password = new();
    private readonly TextBlock _status = new() { TextWrapping = TextWrapping.Wrap, Margin = new Thickness(0, 12, 0, 0) };
    private readonly Button _connect = new() { Content = "Подключиться", IsDefault = true, MinWidth = 120 };
    private readonly Button _cancel = new() { Content = "Отмена", IsCancel = true, MinWidth = 90, Margin = new Thickness(8, 0, 0, 0) };
    private bool _connecting;

    public NetworkCredentialWindow(string share)
    {
        _share = NetworkShareAccess.ShareRoot(share);
        Title = "Подключение к общей папке";
        var panel = new StackPanel { Margin = new Thickness(20) };
        panel.Children.Add(new TextBlock { Text = _share, TextWrapping = TextWrapping.Wrap, FontWeight = FontWeights.SemiBold });
        panel.Children.Add(new TextBlock
        {
            Text = "Введите учётные данные для доступа к сетевой папке. Пароль не сохраняется в настройках приложения.",
            TextWrapping = TextWrapping.Wrap, Margin = new Thickness(0, 8, 0, 12)
        });
        var userLabel = new Label { Content = "Пользователь (ДОМЕН\\имя или имя)", Target = _user };
        userLabel.SetResourceReference(ForegroundProperty, "TitleBarText");
        panel.Children.Add(userLabel);
        panel.Children.Add(_user);
        var passwordLabel = new Label { Content = "Пароль", Target = _password, Margin = new Thickness(0, 8, 0, 0) };
        passwordLabel.SetResourceReference(ForegroundProperty, "TitleBarText");
        panel.Children.Add(passwordLabel);
        panel.Children.Add(_password);
        _status.SetResourceReference(TextBlock.ForegroundProperty, "StatusWarning");
        panel.Children.Add(_status);
        var actions = new StackPanel { Orientation = Orientation.Horizontal, HorizontalAlignment = HorizontalAlignment.Right, Margin = new Thickness(0, 16, 0, 0) };
        _connect.SetResourceReference(StyleProperty, "PrimaryButton");
        actions.Children.Add(_connect);
        actions.Children.Add(_cancel);
        panel.Children.Add(actions);
        DialogContent = new ScrollViewer { Content = panel, VerticalScrollBarVisibility = ScrollBarVisibility.Auto };
        Loaded += (_, _) => _user.Focus();
        _connect.Click += async (_, _) => await ConnectAsync();
        Closing += (_, e) => e.Cancel = _connecting;
        Closed += (_, _) => _password.Clear();
        PreviewKeyDown += (_, e) =>
        {
            if (e.Key != Key.Escape || !_connecting) return;
            e.Handled = true;
        };
    }

    private async Task ConnectAsync()
    {
        if (_connecting) return;
        if (string.IsNullOrWhiteSpace(_user.Text))
        {
            _status.Text = "Введите имя пользователя.";
            _user.Focus();
            return;
        }
        _connecting = true;
        _connect.IsEnabled = _cancel.IsEnabled = CloseButton.IsEnabled = _user.IsEnabled = _password.IsEnabled = false;
        _status.Text = "Подключение…";
        try
        {
            string user = _user.Text.Trim();
            using var password = _password.SecurePassword;
            await Task.Run(() => NetworkShareAccess.Connect(_share, user, password));
            _connecting = false;
            DialogResult = true;
        }
        catch (Exception ex)
        {
            _status.Text = ex.Message;
            _password.Clear();
        }
        finally
        {
            _connecting = false;
            _connect.IsEnabled = _cancel.IsEnabled = CloseButton.IsEnabled = _user.IsEnabled = _password.IsEnabled = true;
        }
    }
}
