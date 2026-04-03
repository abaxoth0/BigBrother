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
            
            _refreshTimer.Start();
        }

        private void OnLogLineReceived(string line)
        {
            System.Diagnostics.Debug.WriteLine($"[MainWindow] OnLogLineReceived: {line}");
            
            if (DataContext is MainViewModel vm)
            {
                System.Diagnostics.Debug.WriteLine($"[MainWindow] vm is not null, adding log");
                
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
                    System.Diagnostics.Debug.WriteLine($"[MainWindow] Dispatcher invoking AddLog");
                    vm.AddLog(level, line);
                });
            }
            else
            {
                System.Diagnostics.Debug.WriteLine($"[MainWindow] DataContext is null or not MainViewModel");
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
                    
                    // Check if client just started (was 0, now has PID)
                    if (status.ClientPid != 0 && _lastClientPid == 0)
                    {
                        System.Diagnostics.Debug.WriteLine($"[Frontend] Client started, requesting log paths...");
                        await StartLogReadersAsync();
                    }
                    _lastClientPid = status.ClientPid;
                }
                catch (System.TimeoutException)
                {
                    // Timeout is expected when server is not available - silently ignore
                    // If client was running before, stop log readers
                    if (_lastClientPid != 0)
                    {
                        System.Diagnostics.Debug.WriteLine($"[Frontend] Connection lost, stopping log readers");
                        _clientLogReader.Stop();
                        _firewallLogReader.Stop();
                    }
                    _lastClientPid = 0;
                }
                catch (System.Exception ex) when (ex is not System.OperationCanceledException)
                {
                    vm.DaemonConnectionStatus = "Отключено";
                    vm.ClientConnectionStatus = "Отключено";
                    System.Diagnostics.Debug.WriteLine($"[Frontend] Status error: {ex.Message}");
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
            try
            {
                var (clientLog, firewallLog) = await _ipcService.GetLogPathAsync();
                System.Diagnostics.Debug.WriteLine($"[MainWindow] Client log: {clientLog}, Firewall log: {firewallLog}");
                
                // Start reading client log
                if (!string.IsNullOrEmpty(clientLog) && File.Exists(clientLog))
                {
                    _clientLogReader.Stop();
                    _clientLogReader.OnNewLine += OnLogLineReceived;
                    _clientLogReader.Start(clientLog, LogSource.Client);
                    System.Diagnostics.Debug.WriteLine($"[MainWindow] Started client log reader");
                }
                else
                {
                    System.Diagnostics.Debug.WriteLine($"[MainWindow] Client log not found: {clientLog}");
                }
                
                // Start reading firewall log
                if (!string.IsNullOrEmpty(firewallLog) && File.Exists(firewallLog))
                {
                    _firewallLogReader.Stop();
                    _firewallLogReader.OnNewLine += OnLogLineReceived;
                    _firewallLogReader.Start(firewallLog, LogSource.Firewall);
                    System.Diagnostics.Debug.WriteLine($"[MainWindow] Started firewall log reader");
                }
                else
                {
                    System.Diagnostics.Debug.WriteLine($"[MainWindow] Firewall log not found: {firewallLog}");
                }
                
                if (DataContext is MainViewModel vm)
                {
                    if (!string.IsNullOrEmpty(clientLog) || !string.IsNullOrEmpty(firewallLog))
                    {
                        vm.AddLog("INFO", $"Чтение логов: client={clientLog}, firewall={firewallLog}");
                    }
                }
            }
            catch (Exception ex) 
            {
                System.Diagnostics.Debug.WriteLine($"[MainWindow] StartLogReadersAsync error: {ex.Message}");
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