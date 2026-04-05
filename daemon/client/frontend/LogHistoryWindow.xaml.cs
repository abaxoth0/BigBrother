using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Windows;
using Lib;

namespace frontend;

public partial class LogHistoryWindow : Window
{
    private readonly string _firewallLogPath;
    private readonly string _clientLogPath;
    private List<LogEntry> _firewallLogs = new();
    private List<LogEntry> _clientLogs = new();

    public string SearchText { get; set; } = "";

    public LogHistoryWindow(string firewallLogPath, string clientLogPath)
    {
        InitializeComponent();
        
        _firewallLogPath = firewallLogPath;
        _clientLogPath = clientLogPath;
        
        SearchTextBox.TextChanged += (s, e) => FilterLogs();
        
        LoadAllLogs();
    }

    private void LoadAllLogs()
    {
        System.Diagnostics.Debug.WriteLine($"[History] Loading firewall: {_firewallLogPath}");
        System.Diagnostics.Debug.WriteLine($"[History] Loading client: {_clientLogPath}");
        
        _firewallLogs = LoadLogFile(_firewallLogPath, LogSource.Firewall);
        _clientLogs = LoadLogFile(_clientLogPath, LogSource.Client);
        
        System.Diagnostics.Debug.WriteLine($"[History] Firewall entries: {_firewallLogs.Count}");
        System.Diagnostics.Debug.WriteLine($"[History] Client entries: {_clientLogs.Count}");
        
        FirewallListBox.ItemsSource = _firewallLogs;
        ClientListBox.ItemsSource = _clientLogs;
        
        FirewallTab.Header = $"Firewall ({_firewallLogs.Count})";
        ClientTab.Header = $"Client ({_clientLogs.Count})";
        
        StatusText.Text = $"Загружено: {_firewallLogs.Count} записей Firewall, {_clientLogs.Count} записей Client";
    }

    private List<LogEntry> LoadLogFile(string path, LogSource source)
    {
        var entries = new List<LogEntry>();
        
        if (string.IsNullOrEmpty(path))
        {
            System.Diagnostics.Debug.WriteLine($"[History] Path is empty for source {source}");
            return entries;
        }
        
        if (!File.Exists(path))
        {
            System.Diagnostics.Debug.WriteLine($"[History] File not found: {path}");
            return entries;
        }
        
        System.Diagnostics.Debug.WriteLine($"[History] Reading file: {path}");
        
        try
        {
            using var fs = new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.ReadWrite | FileShare.Delete);
            var data = new byte[fs.Length];
            fs.Read(data, 0, data.Length);
            
            System.Diagnostics.Debug.WriteLine($"[History] Read {data.Length} bytes from {path}");
            
            var parsed = LogParser.ParseAll(data);
            System.Diagnostics.Debug.WriteLine($"[History] Parsed {parsed.Count} entries from {path}");
            
            foreach (var entry in parsed)
            {
                entry.Source = source;
                entries.Add(entry);
            }
            
            System.Diagnostics.Debug.WriteLine($"[History] Added {entries.Count} entries for source {source}");
        }
        catch (Exception ex)
        {
            System.Diagnostics.Debug.WriteLine($"[History] Error reading {path}: {ex.Message}");
            System.Windows.MessageBox.Show($"Ошибка чтения логов: {ex.Message}", "Ошибка", MessageBoxButton.OK, MessageBoxImage.Error);
        }
        
        return entries;
    }

    private void FilterLogs()
    {
        var searchLower = SearchTextBox.Text?.ToLower() ?? "";
        
        if (string.IsNullOrEmpty(searchLower))
        {
            FirewallListBox.ItemsSource = _firewallLogs;
            ClientListBox.ItemsSource = _clientLogs;
        }
        else
        {
            FirewallListBox.ItemsSource = _firewallLogs
                .Where(l => l.Message.ToLower().Contains(searchLower))
                .ToList();
            ClientListBox.ItemsSource = _clientLogs
                .Where(l => l.Message.ToLower().Contains(searchLower))
                .ToList();
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
                
                // Export current visible logs
                var fw = FirewallListBox.ItemsSource as IEnumerable<LogEntry>;
                var cl = ClientListBox.ItemsSource as IEnumerable<LogEntry>;
                
                if (fw != null)
                {
                    foreach (var log in fw)
                    {
                        writer.WriteLine($"[{log.Timestamp:HH:mm:ss}] [FW] [{log.Level}] {log.Message}");
                    }
                }
                
                if (cl != null)
                {
                    foreach (var log in cl)
                    {
                        writer.WriteLine($"[{log.Timestamp:HH:mm:ss}] [CL] [{log.Level}] {log.Message}");
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