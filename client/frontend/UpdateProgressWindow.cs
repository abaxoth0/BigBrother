using System.Windows;
using System.Windows.Controls;

namespace frontend;

public sealed class UpdateProgressWindow : Window
{
    private readonly TextBlock _status = new() { TextWrapping = TextWrapping.Wrap, Margin = new Thickness(0, 12, 0, 12) };
    private readonly System.Windows.Controls.ProgressBar _progress = new() { IsIndeterminate = true, Height = 8 };
    private bool _finished;

    public UpdateProgressWindow()
    {
        Title = "Обновление BigBrother";
        Width = 500;
        SizeToContent = SizeToContent.Height;
        ResizeMode = ResizeMode.NoResize;
        WindowStartupLocation = WindowStartupLocation.CenterScreen;
        var panel = new StackPanel { Margin = new Thickness(24) };
        panel.Children.Add(_status);
        panel.Children.Add(_progress);
        Content = panel;
        Closing += (_, e) => e.Cancel = !_finished;
    }

    public void Report(string text) => Dispatcher.Invoke(() => _status.Text = text);

    public void Finish(string text)
    {
        _finished = true;
        Report(text);
        _progress.Visibility = Visibility.Collapsed;
        var button = new System.Windows.Controls.Button { Content = "Закрыть", HorizontalAlignment = System.Windows.HorizontalAlignment.Right, Margin = new Thickness(0, 12, 0, 0), Padding = new Thickness(16, 6, 16, 6) };
        button.Click += (_, _) => Close();
        ((StackPanel)Content).Children.Add(button);
    }

    public void Dismiss() { _finished = true; Close(); }
}
