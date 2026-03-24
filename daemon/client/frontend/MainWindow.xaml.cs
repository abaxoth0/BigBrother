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

        public MainWindow()
        {
            InitializeComponent();

            _refreshTimer = new DispatcherTimer
            {
                Interval = TimeSpan.FromSeconds(5)
            };
            _refreshTimer.Tick += async (s, e) => await RefreshStatusAsync();
            _refreshTimer.Start();

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

            _ = RefreshStatusAsync();
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
                    vm.LastUpdate = DateTime.Now;
                }
                catch (System.TimeoutException)
                {
                    // Timeout is expected when server is not available - silently ignore
                }
                catch (System.Exception ex) when (ex is not System.OperationCanceledException)
                {
                    vm.DaemonConnectionStatus = "Отключено";
                    vm.ClientConnectionStatus = "Отключено";
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
