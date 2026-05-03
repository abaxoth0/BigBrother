using System.Collections.ObjectModel;
using System.Windows;
using System.Windows.Input;
using frontend.Services;
using frontend.Views;

namespace frontend.ViewModels;

public class RelayCommand : ICommand
{
    private readonly Action<object?> _execute;
    private readonly Predicate<object?>? _canExecute;

    public RelayCommand(Action<object?> execute, Predicate<object?>? canExecute = null)
    {
        _execute = execute ?? throw new ArgumentNullException(nameof(execute));
        _canExecute = canExecute;
    }

    public bool CanExecute(object? parameter) => _canExecute?.Invoke(parameter) ?? true;

    public void Execute(object? parameter) => _execute(parameter);

    public event EventHandler? CanExecuteChanged
    {
        add => CommandManager.RequerySuggested += value;
        remove => CommandManager.RequerySuggested -= value;
    }

    public void RaiseCanExecuteChanged()
    {
        CommandManager.InvalidateRequerySuggested();
    }
}

public class MainViewModel : ViewModelBase
{
    private readonly IpcService _ipcService;
    private System.Timers.Timer? _refreshTimer;

    private string _serverStatus = "Not Connected";
    private string _uptime = "";
    private int _connectedClientsCount;
    private int _pendingCount;

    private string _activeWhitelist = "";
    private ObservableCollection<WhitelistInfo> _whitelists = new();
    private WhitelistInfo? _selectedWhitelist;

    public MainViewModel()
    {
        _ipcService = new IpcService();

        ConnectedClients = new ObservableCollection<ConnectedClient>();
        PendingRegistrations = new ObservableCollection<PendingRegistration>();
        Whitelists = new ObservableCollection<WhitelistInfo>();
        LogEntries = new ObservableCollection<string>();

        ApproveCommand = new RelayCommand(async o => await ApproveUserAsync(o?.ToString()!), o => o != null);
        RejectCommand = new RelayCommand(async o => await RejectUserAsync(o?.ToString()!), o => o != null);
        DisconnectCommand = new RelayCommand(async o => await DisconnectUserAsync(o?.ToString()!), o => o != null);
        RefreshCommand = new RelayCommand(async _ => await RefreshAllAsync());
        DeleteWhitelistCommand = new RelayCommand(async o => await DeleteWhitelistAsync(o?.ToString()!), o => o != null);
        SetActiveWhitelistCommand = new RelayCommand(async o => await SetActiveWhitelistAsync(o?.ToString()!), o => o != null);
        OpenCreateWhitelistCommand = new RelayCommand(_ => OpenCreateWhitelist());
        OpenEditWhitelistCommand = new RelayCommand(o => OpenEditWhitelist(o as WhitelistInfo), o => o is WhitelistInfo);

        StartAutoRefresh();
    }

    public ObservableCollection<ConnectedClient> ConnectedClients { get; }
    public ObservableCollection<PendingRegistration> PendingRegistrations { get; }
    public ObservableCollection<WhitelistInfo> Whitelists { get; }
    public ObservableCollection<string> LogEntries { get; }

    public string ServerStatus
    {
        get => _serverStatus;
        set => SetProperty(ref _serverStatus, value);
    }

    public string Uptime
    {
        get => _uptime;
        set => SetProperty(ref _uptime, value);
    }

    public int ConnectedClientsCount
    {
        get => _connectedClientsCount;
        set => SetProperty(ref _connectedClientsCount, value);
    }

    public int PendingCount
    {
        get => _pendingCount;
        set => SetProperty(ref _pendingCount, value);
    }

    public string ActiveWhitelist
    {
        get => _activeWhitelist;
        set => SetProperty(ref _activeWhitelist, value);
    }

    public WhitelistInfo? SelectedWhitelist
    {
        get => _selectedWhitelist;
        set
        {
            if (SetProperty(ref _selectedWhitelist, value))
            {
                OnPropertyChanged(nameof(SelectedWhitelistName));
                OnPropertyChanged(nameof(HasSelectedWhitelist));
            }
        }
    }

    public string SelectedWhitelistName => SelectedWhitelist?.Name ?? "";
    public bool HasSelectedWhitelist => SelectedWhitelist != null;

    public ICommand ApproveCommand { get; }
    public ICommand RejectCommand { get; }
    public ICommand DisconnectCommand { get; }
    public ICommand RefreshCommand { get; }
    public ICommand DeleteWhitelistCommand { get; }
    public ICommand SetActiveWhitelistCommand { get; }
    public ICommand OpenCreateWhitelistCommand { get; }
    public ICommand OpenEditWhitelistCommand { get; }

    private void StartAutoRefresh()
    {
        _refreshTimer = new System.Timers.Timer(5000);
        _refreshTimer.Elapsed += async (s, e) => await RefreshAllAsync();
        _refreshTimer.Start();
    }

    public async Task RefreshAllAsync()
    {
        await Task.Run(async () =>
        {
            await RefreshClientsAsync();
            await RefreshPendingAsync();
            await RefreshWhitelistsAsync();
        });
    }

    public async Task RefreshClientsAsync()
    {
        try
        {
            var clients = await _ipcService.GetClientsAsync();
            await System.Windows.Application.Current.Dispatcher.InvokeAsync(() =>
            {
                ConnectedClients.Clear();
                foreach (var client in clients)
                {
                    ConnectedClients.Add(client);
                }
                ConnectedClientsCount = clients.Count;
                ServerStatus = clients.Count > 0 ? "Running" : "Running (No Clients)";
            });
        }
        catch
        {
            ServerStatus = "Not Connected";
        }
    }

    public async Task RefreshPendingAsync()
    {
        try
        {
            var pending = await _ipcService.GetPendingAsync();
            await System.Windows.Application.Current.Dispatcher.InvokeAsync(() =>
            {
                PendingRegistrations.Clear();
                foreach (var p in pending)
                {
                    PendingRegistrations.Add(p);
                }
                PendingCount = pending.Count;
            });
        }
        catch { }
    }

    public async Task RefreshWhitelistsAsync()
    {
        try
        {
            var whitelists = await _ipcService.GetWhitelistsAsync();
            await System.Windows.Application.Current.Dispatcher.InvokeAsync(() =>
            {
                Whitelists.Clear();
                foreach (var wl in whitelists)
                {
                    Whitelists.Add(wl);
                }
                if (whitelists.Count > 0 && string.IsNullOrEmpty(ActiveWhitelist))
                {
                    ActiveWhitelist = whitelists[0].Name;
                }
            });
        }
        catch { }
    }

    private async Task ApproveUserAsync(string? name)
    {
        if (string.IsNullOrEmpty(name)) return;
        await _ipcService.ApproveUserAsync(name);
        await RefreshPendingAsync();
        await RefreshClientsAsync();
    }

    private async Task RejectUserAsync(string? name)
    {
        if (string.IsNullOrEmpty(name)) return;
        await _ipcService.RejectUserAsync(name);
        await RefreshPendingAsync();
    }

    private async Task DisconnectUserAsync(string? name)
    {
        if (string.IsNullOrEmpty(name)) return;
        await _ipcService.DisconnectUserAsync(name);
        await RefreshClientsAsync();
    }

    private async Task DeleteWhitelistAsync(string? name)
    {
        if (string.IsNullOrEmpty(name)) return;
        await _ipcService.DeleteWhitelistAsync(name);
        await RefreshWhitelistsAsync();
    }

    private async Task SetActiveWhitelistAsync(string? name)
    {
        if (string.IsNullOrEmpty(name)) return;
        ActiveWhitelist = name;
        await _ipcService.SetActiveWhitelistAsync(name);
    }

    private void OpenCreateWhitelist()
    {
        var dialog = new WhitelistEditWindow();
        dialog.ViewModel.SetCreateMode();
        dialog.Owner = Application.Current.MainWindow;

        dialog.ViewModel.Saved += async (s, e) =>
        {
            var name = dialog.ViewModel.WhitelistName;
            var entries = dialog.ViewModel.GetEntries();
            await _ipcService.CreateWhitelistAsync(name);
            if (entries.Count > 0)
            {
                await _ipcService.SaveWhitelistAsync(name, entries);
            }
            await RefreshWhitelistsAsync();
        };

        dialog.ShowDialog();
    }

    private void OpenEditWhitelist(WhitelistInfo? whitelist)
    {
        if (whitelist == null) return;

        var dialog = new WhitelistEditWindow();
        dialog.Owner = Application.Current.MainWindow;

        _ = LoadWhitelistForEdit(whitelist.Name, dialog.ViewModel);

        dialog.ViewModel.Saved += async (s, e) =>
        {
            var newName = dialog.ViewModel.WhitelistName;
            var entries = dialog.ViewModel.GetEntries();

            if (dialog.ViewModel.IsEditMode() && dialog.ViewModel.GetOriginalName() != newName)
            {
                await _ipcService.RenameWhitelistAsync(dialog.ViewModel.GetOriginalName(), newName);
            }

            await _ipcService.SaveWhitelistAsync(newName, entries);
            await RefreshWhitelistsAsync();
        };

        dialog.ShowDialog();
    }

    private async Task LoadWhitelistForEdit(string name, WhitelistEditViewModel viewModel)
    {
        try
        {
            var entries = await _ipcService.GetWhitelistEntriesAsync(name);
            await System.Windows.Application.Current.Dispatcher.InvokeAsync(() =>
            {
                viewModel.SetEditMode(name, entries);
            });
        }
        catch { }
    }

    public void Dispose()
    {
        _refreshTimer?.Stop();
        _refreshTimer?.Dispose();
        _ipcService.Dispose();
    }
}
