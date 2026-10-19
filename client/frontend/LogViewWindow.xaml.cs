using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Runtime.InteropServices;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
using frontend.Models;
using frontend.ViewModels;

namespace frontend;

public partial class LogViewWindow : ChromeWindow
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
        Loaded += OnLoaded;
    }

    private async void OnLoaded(object sender, RoutedEventArgs e)
    {
        Loaded -= OnLoaded;
        await _vm.ReloadAsync();
    }

    protected override void OnClosed(EventArgs e)
    {
        _vm.Dispose();
        base.OnClosed(e);
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
                DataContext = src,
                SelectionMode = System.Windows.Controls.SelectionMode.Extended,
                ToolTip = "Shift/Ctrl + щелчок — выбор строк; Ctrl+A — выделить всё; Ctrl+C — копировать",
                ItemTemplate = (DataTemplate)FindResource("LogLineTemplate")
            };
            listBox.CommandBindings.Add(new CommandBinding(ApplicationCommands.Copy, CopyLogs, CanCopyLogs));
            listBox.CommandBindings.Add(new CommandBinding(ApplicationCommands.SelectAll,
                (sender, e) => { ((System.Windows.Controls.ListBox)sender).SelectAll(); e.Handled = true; },
                (sender, e) => { e.CanExecute = ((System.Windows.Controls.ListBox)sender).Items.Count > 0; e.Handled = true; }));
            var menu = new System.Windows.Controls.ContextMenu { Style = (Style)FindResource("LogContextMenuStyle") };
            menu.Items.Add(new System.Windows.Controls.MenuItem
            {
                Header = "Копировать", Command = ApplicationCommands.Copy, CommandTarget = listBox
            });
            menu.Items.Add(new System.Windows.Controls.MenuItem
            {
                Header = "Выделить всё", Command = ApplicationCommands.SelectAll, CommandTarget = listBox
            });
            listBox.ContextMenu = menu;
            listBox.SetValue(VirtualizingPanel.IsVirtualizingProperty, true);
            listBox.SetValue(VirtualizingPanel.VirtualizationModeProperty, VirtualizationMode.Recycling);
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
    }

    private static void CanCopyLogs(object sender, CanExecuteRoutedEventArgs e)
    {
        e.CanExecute = ((System.Windows.Controls.ListBox)sender).SelectedItems.Count > 0;
        e.Handled = true;
    }

    private void CopyLogs(object sender, ExecutedRoutedEventArgs e)
    {
        var listBox = (System.Windows.Controls.ListBox)sender;
        var source = (LogSourceViewModel)listBox.DataContext;
        var selected = listBox.SelectedItems.Cast<Lib.LogEntry>().ToHashSet();
        if (selected.Count == 0) return;
        string text = string.Join(Environment.NewLine, listBox.Items.Cast<Lib.LogEntry>()
            .Where(selected.Contains).Select(entry => FormatLogEntry(entry, source.Label)));
        try
        {
            System.Windows.Clipboard.SetText(text);
        }
        catch (ExternalException)
        {
            _vm.StatusText = "Не удалось скопировать записи. Повторите попытку.";
        }
        e.Handled = true;
    }

    private static string FormatLogEntry(Lib.LogEntry entry, string label) =>
        $"[{entry.LocalTimestamp:yyyy-MM-dd HH:mm:ss}] [{label}] [{entry.Level}] {entry.Message}";

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
                    writer.WriteLine(FormatLogEntry(log, label));
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
