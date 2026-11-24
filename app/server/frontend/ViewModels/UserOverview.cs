using System.ComponentModel;
using frontend.Models;

namespace frontend.ViewModels;

public sealed class UserOverview : ISelectableRow, IDisposable
{
    public event PropertyChangedEventHandler? PropertyChanged;
    public ConnectedClient? Client { get; }
    public PendingRegistration? Pending { get; }
    public ISelectableRow Row => (ISelectableRow?)Client ?? Pending!;
    public string Name => Client?.Name ?? Pending!.Name;
    public string Address => Client?.Address ?? Pending!.Address;
    public bool IsOnline { get; private set; }
    public string State => Pending != null ? "Ожидает регистрации" : IsOnline ? "Подключён" : "Не подключён";
    public string Whitelist => IsOnline ? Client!.Whitelist : "—";
    public string FiltrationText => IsOnline ? Client!.FiltrationText : "—";
    public string Ipv6BlockText => IsOnline ? Client!.Ipv6BlockText : "—";
    public DateTime? LastActivity => IsOnline ? Client!.LastActivity : null;
    public DateTime? CreatedAt => Pending?.CreatedAt;
    public bool IsSelected { get => Row.IsSelected; set => Row.IsSelected = value; }

    public UserOverview(ConnectedClient client) { Client = client; Row.PropertyChanged += OnRowChanged; }
    public UserOverview(PendingRegistration pending) { Pending = pending; Row.PropertyChanged += OnRowChanged; }

    public void SetOnline(bool online)
    {
        if (IsOnline == online) return;
        IsOnline = online;
        foreach (var name in new[] { nameof(IsOnline), nameof(State), nameof(Whitelist), nameof(FiltrationText), nameof(Ipv6BlockText), nameof(LastActivity) })
            PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(name));
    }

    private void OnRowChanged(object? sender, PropertyChangedEventArgs e) => PropertyChanged?.Invoke(this, e);
    public void Dispose() => Row.PropertyChanged -= OnRowChanged;
}
