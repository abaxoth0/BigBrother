using System.Collections.ObjectModel;
using System.IO;
using frontend.Services;

namespace frontend.ViewModels;

public sealed class NetworkNode(string key, bool isServer, bool isPending) : ViewModelBase
{
    private string _name = "";
    private string _address = "";
    private bool _isOnline;
    private bool _statusKnown;
    private bool? _filtrationEnabled;
    private double _x;
    private double _y;
    private double _width = 220;
    private double _height = isServer || isPending ? 104 : 120;

    public string Key { get; } = key;
    public bool IsServer { get; } = isServer;
    public bool IsPending { get; } = isPending;
    public bool IsClient => !IsServer && !IsPending;
    public string Kind => IsServer ? "СЕРВЕР" : IsPending ? "ЗАЯВКА" : "КЛИЕНТ";
    public string Name { get => _name; set => SetProperty(ref _name, value); }
    public string Address { get => _address; set => SetProperty(ref _address, value); }
    public bool IsOnline => _isOnline;
    public bool StatusKnown => _statusKnown;
    public bool? FiltrationEnabled => _filtrationEnabled;
    public string FiltrationText => IsClient
        ? "Фильтрация: " + (FiltrationEnabled is true ? "Вкл" : FiltrationEnabled is false ? "Выкл" : "Нет данных") : "";
    public string Status => !StatusKnown ? "Статус неизвестен" : IsPending ? "Ожидает регистрации"
        : IsServer ? IsOnline ? "Запущен" : "Остановлен / недоступен" : IsOnline ? "Подключён" : "Не подключён";
    public double X => _x;
    public double Y => _y;
    public double Width => _width;
    public double Height => _height;

    internal void SetState(bool online, bool known, bool? filtrationEnabled = null)
    {
        SetProperty(ref _isOnline, online, nameof(IsOnline));
        SetProperty(ref _statusKnown, known, nameof(StatusKnown));
        SetProperty(ref _filtrationEnabled, online && known ? filtrationEnabled : null, nameof(FiltrationEnabled));
        OnPropertyChanged(nameof(Status));
        OnPropertyChanged(nameof(FiltrationText));
    }

    internal void SetPosition(double x, double y)
    {
        SetProperty(ref _x, x, nameof(X));
        SetProperty(ref _y, y, nameof(Y));
    }

    internal void SetSize(double scale)
    {
        SetProperty(ref _width, 220 * scale, nameof(Width));
        SetProperty(ref _height, (IsClient ? 120 : 104) * scale, nameof(Height));
    }
}

public sealed class NetworkSchemeViewModel : ViewModelBase
{
    private readonly NetworkLayoutStore _store;
    private readonly Dictionary<string, NetworkPosition> _positions;
    private bool _available;
    private bool _clientsCurrent;
    private double _nodeScale = 1;
    private string _saveStatus = "";
    public ObservableCollection<NetworkNode> Nodes { get; } = new();
    public ObservableCollection<NetworkNode> Clients { get; } = new();
    public NetworkNode Server { get; } = new("server", true, false);
    public string SaveStatus { get => _saveStatus; private set => SetProperty(ref _saveStatus, value); }
    public string Summary => !_available ? "Нет связи с сервером. Статусы клиентов неизвестны."
        : !_clientsCurrent ? "Статусы клиентов недоступны."
        : $"Подключено: {Clients.Count(n => n.IsOnline)} · Не подключено: {Clients.Count(n => !n.IsOnline && !n.IsPending)} · Ожидают: {Clients.Count(n => n.IsPending)}";
    public double CanvasLeft => Math.Min(0, Nodes.Min(n => n.X));
    public double CanvasTop => Math.Min(0, Nodes.Min(n => n.Y));
    public double CanvasOffsetX => -CanvasLeft;
    public double CanvasOffsetY => -CanvasTop;
    public double CanvasWidth => Math.Max(1100, Nodes.Max(n => n.X + n.Width) + 80 - CanvasLeft);
    public double CanvasHeight => Math.Max(700, Nodes.Max(n => n.Y + n.Height) + 80 - CanvasTop);

    public NetworkSchemeViewModel() : this(new NetworkLayoutStore(Path.Combine(
        Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "BigBrother", "Server", "network-layout.json"))) { }

    public NetworkSchemeViewModel(NetworkLayoutStore store)
    {
        _store = store;
        _positions = store.Load();
        Server.Name = Environment.MachineName;
        Place(Server);
        Nodes.Add(Server);
    }

    public void SetServer(string name, string address)
    {
        Server.Name = string.IsNullOrWhiteSpace(name) ? Environment.MachineName : name;
        Server.Address = address;
    }

    public void SetAvailable(bool available)
    {
        _available = available;
        Server.SetState(available, true);
        if (!available) MarkClientsUnknown();
        OnPropertyChanged(nameof(Summary));
    }

    public void MarkClientsUnknown()
    {
        _clientsCurrent = false;
        foreach (var node in Clients) node.SetState(false, false);
        OnPropertyChanged(nameof(Summary));
    }

    public void Refresh(IEnumerable<UserOverview> users, bool? clientsCurrent = null)
    {
        if (clientsCurrent.HasValue) _clientsCurrent = clientsCurrent.Value;
        var previous = Clients.ToDictionary(n => n.Key);
        var current = new List<NetworkNode>();
        foreach (var user in users)
        {
            var key = (user.Pending == null ? "client:" : "pending:") + user.Name;
            if (!previous.TryGetValue(key, out var node))
            {
                node = new NetworkNode(key, false, user.Pending != null);
                if (!_positions.ContainsKey(key) && user.Pending == null && _positions.TryGetValue("pending:" + user.Name, out var pendingPosition))
                    _positions[key] = pendingPosition;
                Place(node);
                Nodes.Add(node);
            }
            node.Name = user.Name;
            node.Address = user.Address;
            node.SetState(user.IsOnline && _available && _clientsCurrent, _available && (node.IsPending || _clientsCurrent),
                user.Client?.Policy?.FiltrationEnabled);
            current.Add(node);
        }
        if (!_clientsCurrent)
            current.AddRange(Clients.Where(n => !n.IsPending && !current.Contains(n)));
        foreach (var removed in Clients.Except(current).ToList()) Nodes.Remove(removed);
        UserTableView.Update(Clients, current);
        NotifyLayout();
        OnPropertyChanged(nameof(Summary));
    }

    private void Place(NetworkNode node)
    {
        node.SetSize(_nodeScale);
        if (_positions.TryGetValue(node.Key, out var position)) node.SetPosition(position.X, position.Y);
        else
        {
            int slot = 0;
            double x, y;
            do
            {
                x = 60 + (node.IsServer ? (slot + 1) % 3 : slot % 3) * (node.Width + 70);
                y = 60 + (node.IsServer ? slot / 3 : 1 + slot / 3) * (node.Height + 80);
                slot++;
            }
            while (Nodes.Any(n => x < n.X + n.Width + 20 && x + node.Width + 20 > n.X &&
                y < n.Y + n.Height + 20 && y + node.Height + 20 > n.Y) ||
                _positions.Values.Any(p => x < p.X + node.Width + 20 && x + node.Width + 20 > p.X &&
                    y < p.Y + node.Height + 20 && y + node.Height + 20 > p.Y));
            node.SetPosition(x, y);
            _positions[node.Key] = new(x, y);
        }
    }

    public void Move(NetworkNode node, double x, double y)
    {
        if (!Nodes.Contains(node) || !double.IsFinite(x) || !double.IsFinite(y)) return;
        node.SetPosition(Math.Clamp(x, -100000, 100000), Math.Clamp(y, -100000, 100000));
        _positions[node.Key] = new(node.X, node.Y);
        NotifyLayout();
    }

    public void SetNodeScale(double scale)
    {
        _nodeScale = Math.Clamp(scale, 0.7, 2);
        foreach (var node in Nodes) node.SetSize(_nodeScale);
        NotifyLayout();
    }

    public void ResetLayout()
    {
        _positions.Clear();
        for (int i = 0; i < Nodes.Count; i++)
            Move(Nodes[i], 60 + (i == 0 ? 1 : (i - 1) % 3) * (Server.Width + 70),
                60 + (i == 0 ? 0 : 1 + (i - 1) / 3) * (Server.Height + 80));
        SaveLayout();
    }

    public void SaveLayout()
    {
        try { _store.Save(_positions); SaveStatus = "Расположение сохранено"; }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException)
        { SaveStatus = "Не удалось сохранить расположение: " + ex.Message; }
    }

    private void NotifyLayout()
    {
        OnPropertyChanged(nameof(CanvasLeft));
        OnPropertyChanged(nameof(CanvasTop));
        OnPropertyChanged(nameof(CanvasOffsetX));
        OnPropertyChanged(nameof(CanvasOffsetY));
        OnPropertyChanged(nameof(CanvasWidth));
        OnPropertyChanged(nameof(CanvasHeight));
    }
}
