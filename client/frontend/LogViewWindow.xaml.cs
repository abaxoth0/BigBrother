using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Windows;
using System.Windows.Controls;
using frontend.Models;
using frontend.ViewModels;

namespace frontend;

public partial class LogViewWindow : Window
{
    private readonly LogViewViewModel _vm;
    private readonly List<System.Windows.Controls.ListBox> _listBoxes = new();
    private readonly List<System.Windows.Controls.TabItem> _tabs = new();

    public LogViewWindow(List<frontend.Models.LogFileSource> sources)
    {
        InitializeComponent();

        _vm = new LogViewViewModel(sources);
        _vm.ReloadRequested += () => RefreshTabBindings();
        _vm.ExportRequested += Export;
        _vm.CloseRequested += () => Close();
        DataContext = _vm;

        BuildTabs();
        RefreshTabBindings();
    }

    private void BuildTabs()
    {
        foreach (var src in _vm.Sources)
        {
            var tab = new System.Windows.Controls.TabItem { Header = src.Label };
            var listBox = new System.Windows.Controls.ListBox
            {
                FontFamily = new System.Windows.Media.FontFamily("Consolas"),
                FontSize = 12,
                ItemTemplate = (DataTemplate)FindResource("LogLineTemplate")
            };
            listBox.SetValue(VirtualizingPanel.IsVirtualizingProperty, true);
            tab.Content = listBox;
            LogTabs.Items.Add(tab);

            _tabs.Add(tab);
            _listBoxes.Add(listBox);
        }
    }

    private void RefreshTabBindings()
    {
        for (int i = 0; i < _vm.Sources.Count; i++)
        {
            var src = _vm.Sources[i];
            _listBoxes[i].ItemsSource = src.FilteredEntries;
            _tabs[i].Header = $"{src.Label} ({src.AllEntries.Count})";
        }
        StatusText.Text = _vm.StatusText;
    }

    private void Export()
    {
        var dialog = new Microsoft.Win32.SaveFileDialog
        {
            Filter = "Text files (*.txt)|*.txt|All files (*.*)|*.*",
            FileName = $"logs_{DateTime.Now:yyyyMMdd_HHmmss}.txt"
        };

        if (dialog.ShowDialog() != true) return;

        try
        {
            using var writer = new StreamWriter(dialog.FileName);
            for (int i = 0; i < _vm.Sources.Count; i++)
            {
                var logs = _listBoxes[i].ItemsSource as IEnumerable<Lib.LogEntry>;
                if (logs == null) continue;

                var label = _vm.Sources[i].Label;
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
