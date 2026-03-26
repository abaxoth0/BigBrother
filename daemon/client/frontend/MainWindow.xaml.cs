using System;
using System.IO;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Threading;
using frontend.ViewModels;
using frontend.Services;

namespace frontend
{
    public partial class MainWindow : Window
    {
        private readonly DispatcherTimer _refreshTimer;
        private readonly IpcService _ipcService = new();
        private readonly ServiceManager _serviceManager = new();
        private readonly LogReader _logReader = new();

        public MainWindow()
        {
            InitializeComponent();

            _refreshTimer = new DispatcherTimer
            {
                Interval = TimeSpan.FromSeconds(5)
            };
            _refreshTimer.Tick += async (s, e) => await RefreshStatusAsync();

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
            
            // Then try to get log path from backend
            try
            {
                var logPath = await _ipcService.GetLogPathAsync();
                if (!string.IsNullOrEmpty(logPath) && File.Exists(logPath))
                {
                    _logReader.OnNewLine += OnLogLineReceived;
                    _logReader.Start(logPath);
                    if (DataContext is MainViewModel vm)
                    {
                        vm.AddLog("INFO", $"Чтение логов: {logPath}");
                    }
                }
            }
            catch { }

            _refreshTimer.Start();
        }

        private void OnLogLineReceived(string line)
        {
            if (DataContext is MainViewModel vm)
            {
                string level = "INFO";
                if (line.Contains("[ERROR]") || line.Contains("ERROR"))
                    level = "ERROR";
                else if (line.Contains("[WARNING]") || line.Contains("WARN"))
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
                    vm.HostName = status.ClientName;
                    vm.IpAddress = status.IpAddress;
                    vm.ClientPid = status.ClientPid;
                    vm.LastUpdate = DateTime.Now;
                    
                    System.Diagnostics.Debug.WriteLine($"[Frontend] Status: Daemon={status.DaemonStatus}, ClientPID={status.ClientPid}");
                }
                catch (System.TimeoutException)
                {
                    // Timeout is expected when server is not available - silently ignore
                }
                catch (System.Exception ex) when (ex is not System.OperationCanceledException)
                {
                    vm.DaemonConnectionStatus = "Отключено";
                    vm.ClientConnectionStatus = "Отключено";
                    System.Diagnostics.Debug.WriteLine($"[Frontend] Status error: {ex.Message}");
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
    }
}