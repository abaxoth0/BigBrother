using System.Collections.ObjectModel;
using frontend.Models;

namespace frontend.ViewModels;

internal static class UserTableView
{
    public static void UpdateRegistered(ObservableCollection<ConnectedClient> registered,
        ObservableCollection<ConnectedClient> connected, IEnumerable<ConnectedClient> registry, IEnumerable<ConnectedClient> liveClients)
    {
        var live = liveClients.ToDictionary(c => c.Name);
        var previous = registered.ToDictionary(c => c.Name);
        var current = new List<ConnectedClient>();
        ConnectedClient Merge(ConnectedClient incoming, ConnectedClient? online)
        {
            var row = previous.GetValueOrDefault(incoming.Name) ?? incoming;
            row.Address = online?.Address ?? incoming.Address;
            row.Status = online == null ? "Не подключён" : "Подключён";
            row.Policy = online?.Policy;
            row.WhitelistCurrent = online?.WhitelistCurrent;
            row.LastActivity = online?.LastActivity;
            return row;
        }
        foreach (var incoming in registry) current.Add(Merge(incoming, live.GetValueOrDefault(incoming.Name)));
        foreach (var online in live.Values)
            if (current.All(c => c.Name != online.Name)) current.Add(Merge(online, online));
        Update(registered, current);
        Update(connected, current.Where(c => live.ContainsKey(c.Name)));
    }

    public static void UpdateOverview(ObservableCollection<UserOverview> rows, IEnumerable<ConnectedClient> registered,
        IEnumerable<ConnectedClient> connected, IEnumerable<PendingRegistration> pending)
    {
        var previous = rows.ToDictionary(r => r.Row);
        var online = new HashSet<ConnectedClient>(connected);
        var current = new List<UserOverview>();
        foreach (var client in registered)
        {
            var row = previous.GetValueOrDefault(client) ?? new UserOverview(client);
            row.SetOnline(online.Contains(client));
            current.Add(row);
        }
        foreach (var request in pending)
            current.Add(previous.GetValueOrDefault(request) ?? new UserOverview(request));
        foreach (var removed in rows.Except(current).ToList()) removed.Dispose();
        Update(rows, current);
    }

    public static List<T> SelectedOrCurrent<T>(IEnumerable<T> rows, T? current, bool checkedRowsExist = false) where T : class, ISelectableRow
    {
        var selected = rows.Where(row => row.IsSelected).ToList();
        if (selected.Count == 0 && !checkedRowsExist && current != null) selected.Add(current);
        return selected;
    }

    public static void Update<T>(ObservableCollection<T> rows, IEnumerable<T> visibleRows) where T : class
    {
        var visible = visibleRows.ToList();
        var remaining = new HashSet<T>(visible);
        for (var i = rows.Count - 1; i >= 0; i--)
            if (!remaining.Contains(rows[i]))
                rows.RemoveAt(i);

        for (var i = 0; i < visible.Count; i++)
        {
            if (i < rows.Count && ReferenceEquals(rows[i], visible[i])) continue;
            var previousIndex = rows.IndexOf(visible[i]);
            if (previousIndex >= 0)
                rows.Move(previousIndex, i);
            else
                rows.Insert(i, visible[i]);
        }
    }
}
