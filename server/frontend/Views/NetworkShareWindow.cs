using System.IO;
using System.Windows;
using System.Windows.Controls;
using frontend.Services;

namespace frontend.Views;

public sealed class NetworkShareWindow : Window
{
    private readonly ComboBox _path = new() { IsEditable = true, MinWidth = 450 };
    private readonly TextBlock _status = new() { TextWrapping = TextWrapping.Wrap, Margin = new Thickness(0, 10, 0, 0) };
    private readonly Button _open = new() { Content = "Открыть папку", IsDefault = true, HorizontalAlignment = HorizontalAlignment.Right, Margin = new Thickness(0, 12, 0, 0) };
    public string? SelectedFile { get; private set; }

    public NetworkShareWindow(string current)
    {
        Title = "Общая папка обновлений";
        Width = 640;
        SizeToContent = SizeToContent.Height;
        ResizeMode = ResizeMode.NoResize;
        WindowStartupLocation = WindowStartupLocation.CenterOwner;
        _path.ItemsSource = NetworkShareAccess.SavedLocations();
        try
        {
            var path = NetworkShareAccess.ToUnc(current);
            _path.Text = path.EndsWith(".exe", StringComparison.OrdinalIgnoreCase) ? Path.GetDirectoryName(path) : path;
        }
        catch (IOException) { }
        var panel = new StackPanel { Margin = new Thickness(20) };
        panel.Children.Add(new TextBlock {
            Text = @"Введите \\сервер\общая_папка или выберите сохранённую сетевую папку. При необходимости Windows запросит учётные данные.",
            TextWrapping = TextWrapping.Wrap, Margin = new Thickness(0, 0, 0, 10)
        });
        panel.Children.Add(_path);
        panel.Children.Add(_status);
        panel.Children.Add(_open);
        Content = panel;
        _open.Click += async (_, _) => await OpenAsync();
    }

    private async Task OpenAsync()
    {
        _open.IsEnabled = false;
        try
        {
            var folder = NetworkShareAccess.ToUnc(_path.Text);
            _status.Text = "Подключение к общей папке…";
            await NetworkShareAccess.ReadAsync(folder, () => {
                using var entries = Directory.EnumerateFileSystemEntries(folder).GetEnumerator();
                entries.MoveNext();
                return true;
            }, NetworkShareDialog.ConnectAsync);
            _status.Text = "";
            var dialog = new Microsoft.Win32.OpenFileDialog {
                Title = "Выберите пакет обновления клиента", Filter = "Пакет обновления (*.exe)|*.exe",
                InitialDirectory = folder, CheckFileExists = true, Multiselect = false
            };
            if (dialog.ShowDialog(this) != true) return;
            SelectedFile = NetworkShareAccess.ToUnc(dialog.FileName);
            DialogResult = true;
        }
        catch (Exception ex) { _status.Text = ex.Message; }
        finally { _open.IsEnabled = true; }
    }
}
