using System;
using System.IO;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Threading;
using frontend.ViewModels;
using frontend.Services;
using Lib;

namespace frontend
{
    public partial class MainWindow : Window
    {
        private readonly DispatcherTimer _refreshTimer;
        private readonly IpcService _ipcService = new();
        private readonly ServiceManager _serviceManager = new();
        private readonly LogReader _clientLogReader = new();
        private readonly LogReader _firewallLogReader = new();
        private int _lastClientPid = 0;
        private string _lastClientLogPath = "";
        private string _lastFirewallLogPath = "";

        public MainWindow()
        {
            InitializeComponent();

            _refreshTimer = new DispatcherTimer
            {
                Interval = TimeSpan.FromSeconds(10)
            };
            _refreshTimer.Tick += async (s, e) => await RefreshStatusAsync();

            var logFlushTimer = new DispatcherTimer
            {
                Interval = TimeSpan.FromMilliseconds(500)
            };
            logFlushTimer.Tick += (s, e) =>
            {
                if (DataContext is MainViewModel vm)
                {
                    vm.FlushPendingLogs();
                }
            };
            logFlushTimer.Start();

            if (DataContext is MainViewModel vm)
            {
                vm.ScrollToBottomRequested += () =>
                {
                    if (LogsListBox.Items.Count > 0)
                    {
                        LogsListBox.ScrollIntoView(LogsListBox.Items[^1]);
                    }
                };
            }

            _ = InitializeAsync();
        }

        private async Task InitializeAsync()
        {
            // First get status to see if we can connect
            await RefreshStatusAsync();
            
            _refreshTimer.Start();
        }

        private void OnLogLineReceived(string line)
        {
            if (DataContext is MainViewModel vm)
            {
                string level = "INFO";
                if (line.Contains("[ERROR]") || line.Contains("ERROR"))
                    level = "ERROR";
                else if (line.Contains("[WARNING]") || line.Contains("[WARN]"))
                    level = "WARNING";
                else if (line.Contains("[DNS]"))
                    level = "DNS";
                else if (line.Contains("[BLOCKED]"))
                    level = "BLOCKED";

                System.Windows.Application.Current.Dispatcher.BeginInvoke(() =>
                {
                    vm.AddLog(level, line);
                });
            }
        }

        private async void RefreshStatus_Click(object sender, RoutedEventArgs e)
        {
            await RefreshStatusAsync();
        }

        private async Task RefreshStatusAsync()
        {
            if (DataContext is MainViewModel vm)
            {
                try
                {
                    var status = await _ipcService.GetStatusAsync();
                    vm.DaemonConnectionStatus = status.DaemonStatus == "RUNNING" ? "Подключено" : "Отключено";
                    vm.DaemonStatus = status.DaemonStatus == "RUNNING" ? "Запущен" : "Остановлен";
                    vm.ClientConnectionStatus = status.IsConnected ? "Подключено" : "Отключено";
                    vm.ClientStatus = status.IsConnected ? "Запущен" : "Остановлен";
                    vm.ServerStatus = status.IsServerRunning ? "Запущен" : "Остановлен";
                    vm.ServerConnectionStatus = status.IsServerSessionActive ? "Подключено" : "Отключено";
                    vm.HostName = status.ClientName;
                    vm.IpAddress = status.IpAddress;
                    vm.ClientPid = status.ClientPid;
                    vm.LastUpdate = DateTime.Now;

                    if (status.ClientPid != 0 && _lastClientPid == 0)
                    {
                        await StartLogReadersAsync();
                    }
                    _lastClientPid = status.ClientPid;
                }
                catch (System.TimeoutException)
                {
                    if (_lastClientPid != 0)
                    {
                        _clientLogReader.Stop();
                        _firewallLogReader.Stop();
                    }
                    _lastClientPid = 0;
                    vm.ServerConnectionStatus = "Отключено";
                    vm.ServerStatus = "Остановлен";
                }
                catch (System.Exception ex) when (ex is not System.OperationCanceledException)
                {
                    vm.DaemonConnectionStatus = "Отключено";
                    vm.ClientConnectionStatus = "Отключено";
                    vm.ServerConnectionStatus = "Отключено";
                    vm.ServerStatus = "Остановлен";
                    if (_lastClientPid != 0)
                    {
                        _clientLogReader.Stop();
                        _firewallLogReader.Stop();
                    }
                    _lastClientPid = 0;
                }
            }
        }
        
        private async Task StartLogReadersAsync()
        {
            string clientLog = "";
            string firewallLog = "";
            
            try
            {
                var paths = await _ipcService.GetLogPathAsync();
                clientLog = paths.clientLog;
                firewallLog = paths.firewallLog;
            }
            catch { }
            
            // If IPC failed, try to use previous paths
            if (string.IsNullOrEmpty(clientLog))
                clientLog = _lastClientLogPath;
            if (string.IsNullOrEmpty(firewallLog))
                firewallLog = _lastFirewallLogPath;
            
            // Save for next time
            if (!string.IsNullOrEmpty(clientLog))
                _lastClientLogPath = clientLog;
            if (!string.IsNullOrEmpty(firewallLog))
                _lastFirewallLogPath = firewallLog;
            
            if (!string.IsNullOrEmpty(clientLog) && File.Exists(clientLog))
            {
                _clientLogReader.Stop();
                _clientLogReader.OnNewLine -= OnLogLineReceived;
                _clientLogReader.OnNewLine += OnLogLineReceived;
                _clientLogReader.Start(clientLog, LogSource.Client);
            }
            
            if (!string.IsNullOrEmpty(firewallLog) && File.Exists(firewallLog))
            {
                _firewallLogReader.Stop();
                _firewallLogReader.OnNewLine -= OnLogLineReceived;
                _firewallLogReader.OnNewLine += OnLogLineReceived;
                _firewallLogReader.Start(firewallLog, LogSource.Firewall);
            }
            
            if (DataContext is MainViewModel vm)
            {
                if (!string.IsNullOrEmpty(clientLog) || !string.IsNullOrEmpty(firewallLog))
                {
                    vm.AddLog("INFO", $"Чтение логов: client={clientLog}, firewall={firewallLog}");
                }
            }
        }

        private async void StartDaemon_Click(object sender, RoutedEventArgs e)
        {
            if (DataContext is MainViewModel vm)
            {
                vm.AddLog("INFO", "Запуск демона...");
                try
                {
                    var result = await _serviceManager.StartServiceAsync("BigBrother");
                    vm.AddLog(result ? "INFO" : "ERROR", result ? "Демон запущен" : "Не удалось запустить демон");
                    await Task.Delay(2000);
                    await RefreshStatusAsync();
                }
                catch (System.Exception ex)
                {
                    vm.AddLog("ERROR", $"Ошибка: {ex.Message}");
                }
            }
        }

        private async void StopDaemon_Click(object sender, RoutedEventArgs e)
        {
            if (DataContext is MainViewModel vm)
            {
                vm.AddLog("INFO", "Остановка демона...");
                try
                {
                    var result = await _serviceManager.StopServiceAsync("BigBrother");
                    vm.AddLog(result ? "INFO" : "ERROR", result ? "Демон остановлен" : "Не удалось остановить демон");
                    await RefreshStatusAsync();
                }
                catch (System.Exception ex)
                {
                    vm.AddLog("ERROR", $"Ошибка: {ex.Message}");
                }
            }
        }

        private async void RestartDaemon_Click(object sender, RoutedEventArgs e)
        {
            if (DataContext is MainViewModel vm)
            {
                vm.AddLog("INFO", "Рестарт демона...");
                try
                {
                    var result = await _serviceManager.RestartServiceAsync("BigBrother");
                    vm.AddLog(result ? "INFO" : "ERROR", result ? "Демон перезапущен" : "Не удалось перезапустить демон");
                    await Task.Delay(3000);
                    await RefreshStatusAsync();
                }
                catch (System.Exception ex)
                {
                    vm.AddLog("ERROR", $"Ошибка: {ex.Message}");
                }
            }
        }

        private async void RestartClient_Click(object sender, RoutedEventArgs e)
        {
            if (DataContext is MainViewModel vm)
            {
                vm.AddLog("INFO", "Перезапуск клиента...");
                var result = await _ipcService.RestartClientAsync();
                vm.AddLog(result ? "INFO" : "ERROR", result ? "Клиент перезапущен" : "Не удалось перезапустить клиент");
                await Task.Delay(2000);
                await RefreshStatusAsync();
            }
        }

        private async void OpenHistory_Click(object sender, RoutedEventArgs e)
        {
            string clientLog = _lastClientLogPath;
            string firewallLog = _lastFirewallLogPath;
            
            // Only try IPC if we have a valid client PID (connected)
            if (_lastClientPid != 0)
            {
                try
                {
                    var paths = await _ipcService.GetLogPathAsync();
                    if (!string.IsNullOrEmpty(paths.clientLog))
                        clientLog = paths.clientLog;
                    if (!string.IsNullOrEmpty(paths.firewallLog))
                        firewallLog = paths.firewallLog;
                }
                catch { }
            }
            
            // Save for next time
            if (!string.IsNullOrEmpty(clientLog))
                _lastClientLogPath = clientLog;
            if (!string.IsNullOrEmpty(firewallLog))
                _lastFirewallLogPath = firewallLog;
            
            if (string.IsNullOrEmpty(clientLog) && string.IsNullOrEmpty(firewallLog))
            {
                System.Windows.MessageBox.Show("Нет доступных логов", "Ошибка", MessageBoxButton.OK, MessageBoxImage.Warning);
                return;
            }
            
            var sources = new List<LogFileSource>();
            
            if (!string.IsNullOrEmpty(firewallLog))
            {
                sources.Add(new LogFileSource
                {
                    Label = "Firewall",
                    FilePath = firewallLog,
                    Source = LogSource.Firewall
                });
            }
            
            if (!string.IsNullOrEmpty(clientLog))
            {
                sources.Add(new LogFileSource
                {
                    Label = "Client",
                    FilePath = clientLog,
                    Source = LogSource.Client
                });
            }
            
            var historyWindow = new LogViewWindow(sources) { Owner = this };
            historyWindow.Show();
        }

        private void OpenArchivedLog_Click(object sender, RoutedEventArgs e)
        {
            var dialog = new Microsoft.Win32.OpenFileDialog
            {
                Filter = "Binary log files (*.binlog)|*.binlog|All files (*.*)|*.*",
                Multiselect = true,
                Title = "Выберите файлы логов"
            };

            if (dialog.ShowDialog() != true || dialog.FileNames.Length == 0)
                return;

            var sources = new List<LogFileSource>();
            foreach (var path in dialog.FileNames)
            {
                var fileName = System.IO.Path.GetFileNameWithoutExtension(path);
                var label = fileName.Contains("firewall") ? "Firewall"
                    : fileName.Contains("client") ? "Client"
                    : fileName;

                sources.Add(new LogFileSource
                {
                    Label = label,
                    FilePath = path,
                    Source = LogSource.Unknown
                });
            }

            var viewWindow = new LogViewWindow(sources) { Owner = this };
            viewWindow.Show();
        }

        protected override void OnClosed(EventArgs e)
        {
            _refreshTimer?.Stop();
            _ipcService.Dispose();
            _clientLogReader.Stop();
            _firewallLogReader.Stop();
            if (DataContext is IDisposable disposable)
                disposable.Dispose();
            base.OnClosed(e);
        }
    }
}