using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Media;
using System.Windows.Data;
using Lib;

namespace frontend;

public class LogFileSource
{
    public string Label { get; set; } = "";
    public string FilePath { get; set; } = "";
    public LogSource Source { get; set; } = LogSource.Unknown;
}

public partial class LogViewWindow : Window
{
    private readonly List<LogFileSource> _sources;
    private readonly List<List<LogEntry>> _allLogs = new();
    private readonly List<System.Windows.Controls.ListBox> _listBoxes = new();
    private readonly List<TabItem> _tabs = new();

    public string SearchText { get; set; } = "";

    public LogViewWindow(List<LogFileSource> sources)
    {
        InitializeComponent();
        _sources = sources;

        SearchTextBox.TextChanged += (s, e) => FilterLogs();

        BuildTabs();
        LoadAllLogs();
    }

    private void BuildTabs()
    {
        foreach (var src in _sources)
        {
            var tab = new TabItem { Header = src.Label };
            var listBox = new System.Windows.Controls.ListBox
            {
                FontFamily = new System.Windows.Media.FontFamily("Consolas"),
                FontSize = 12,
            };
            listBox.SetValue(System.Windows.Controls.VirtualizingPanel.IsVirtualizingProperty, true);
            listBox.ItemTemplate = new DataTemplate();
            listBox.ItemTemplate.VisualTree = new FrameworkElementFactory(typeof(TextBlock));
            listBox.ItemTemplate.VisualTree.SetBinding(TextBlock.TextProperty, new System.Windows.Data.Binding("Message"));

            tab.Content = listBox;
            LogTabs.Items.Add(tab);

            _tabs.Add(tab);
            _listBoxes.Add(listBox);
            _allLogs.Add(new List<LogEntry>());
        }
    }

    private void LoadAllLogs()
    {
        int totalEntries = 0;

        for (int i = 0; i < _sources.Count; i++)
        {
            var src = _sources[i];
            var entries = LoadLogFile(src.FilePath, src.Source, src.Label);
            _allLogs[i] = entries;
            _listBoxes[i].ItemsSource = entries;
            _tabs[i].Header = $"{src.Label} ({entries.Count})";
            totalEntries += entries.Count;
        }

        StatusText.Text = $"Загружено: {totalEntries} записей из {_sources.Count} файлов";
    }

    private List<LogEntry> LoadLogFile(string path, LogSource source, string label)
    {
        var entries = new List<LogEntry>();

        if (string.IsNullOrEmpty(path))
        {
            System.Diagnostics.Debug.WriteLine($"[LogView] Path is empty for {label}");
            return entries;
        }

        if (!File.Exists(path))
        {
            System.Windows.MessageBox.Show($"Файл не найден:\n{path}", "Ошибка", MessageBoxButton.OK, MessageBoxImage.Error);
            return entries;
        }

        var fileInfo = new FileInfo(path);
        if (fileInfo.Length == 0)
        {
            System.Windows.MessageBox.Show($"Файл пуст:\n{path}", "Предупреждение", MessageBoxButton.OK, MessageBoxImage.Warning);
            return entries;
        }

        try
        {
            using var fs = new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.ReadWrite | FileShare.Delete);
            var data = new byte[fs.Length];
            fs.ReadExactly(data, 0, data.Length);

            var parsed = LogParser.ParseAll(data);

            if (parsed.Count == 0)
            {
                System.Windows.MessageBox.Show(
                    $"Файл содержит данные, но не удалось распознать ни одной записи.\n\n" +
                    $"Файл: {path}\n" +
                    $"Размер: {fileInfo.Length} байт\n\n" +
                    $"Возможно, файл повреждён или имеет неверный формат.",
                    "Ошибка чтения", MessageBoxButton.OK, MessageBoxImage.Error);
                return entries;
            }

            foreach (var entry in parsed)
            {
                entry.Source = source;
                entries.Add(entry);
            }
        }
        catch (Exception ex)
        {
            System.Windows.MessageBox.Show(
                $"Ошибка чтения файла:\n{path}\n\n{ex.Message}",
                "Ошибка", MessageBoxButton.OK, MessageBoxImage.Error);
        }

        return entries;
    }

    private void FilterLogs()
    {
        var searchLower = SearchTextBox.Text?.ToLower() ?? "";

        for (int i = 0; i < _sources.Count; i++)
        {
            if (string.IsNullOrEmpty(searchLower))
            {
                _listBoxes[i].ItemsSource = _allLogs[i];
            }
            else
            {
                _listBoxes[i].ItemsSource = _allLogs[i]
                    .Where(l => l.Message.ToLower().Contains(searchLower))
                    .ToList();
            }
        }
    }

    private void Reload_Click(object sender, RoutedEventArgs e)
    {
        LoadAllLogs();
        FilterLogs();
    }

    private void Export_Click(object sender, RoutedEventArgs e)
    {
        var dialog = new Microsoft.Win32.SaveFileDialog
        {
            Filter = "Text files (*.txt)|*.txt|All files (*.*)|*.*",
            FileName = $"logs_{DateTime.Now:yyyyMMdd_HHmmss}.txt"
        };

        if (dialog.ShowDialog() == true)
        {
            try
            {
                using var writer = new StreamWriter(dialog.FileName);

                for (int i = 0; i < _sources.Count; i++)
                {
                    var logs = _listBoxes[i].ItemsSource as IEnumerable<LogEntry>;
                    if (logs == null) continue;

                    var label = _sources[i].Label;
                    foreach (var log in logs)
                    {
                        writer.WriteLine($"[{log.Timestamp:HH:mm:ss}] [{label}] [{log.Level}] {log.Message}");
                    }
                }

                System.Windows.MessageBox.Show($"Логи сохранены: {dialog.FileName}", "Успех", MessageBoxButton.OK, MessageBoxImage.Information);
            }
            catch (Exception ex)
            {
                System.Windows.MessageBox.Show($"Ошибка экспорта: {ex.Message}", "Ошибка", MessageBoxButton.OK, MessageBoxImage.Error);
            }
        }
    }

    private void Close_Click(object sender, RoutedEventArgs e)
    {
        Close();
    }
}
