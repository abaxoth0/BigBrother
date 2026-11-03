using System.Collections.ObjectModel;
using System.Collections.Specialized;
using frontend.Models;
using frontend.ViewModels;

internal static class UserOverviewTests
{
    public static void Run()
    {
        var registered = new ObservableCollection<ConnectedClient>();
        var connected = new ObservableCollection<ConnectedClient>();
        var overview = new ObservableCollection<UserOverview>();
        ConnectedClient[] Registry() => [new() { Name = "alice", Address = "10.0.0.1" },
            new() { Name = "offline:клиент", Address = "[::1]:1984" }];
        ConnectedClient Live() => new() { Name = "alice", Address = "[2001:db8::1]:1984",
            Policy = new ClientPolicyStatus { BlockIpv6 = true, CanSetIpv6Block = true },
            WhitelistCurrent = true, LastActivity = new DateTime(2026, 10, 22, 12, 30, 0) };
        var request = new PendingRegistration { Name = "alice", Address = "10.0.0.3", CreatedAt = DateTime.Now };
        UserTableView.UpdateRegistered(registered, connected, Registry(), [Live()]);
        UserTableView.UpdateOverview(overview, registered, connected, [request]);
        Check(registered.Count == 2 && connected.Count == 1 && overview.Count == 3,
            "Overview omitted offline users or pending registrations");
        var online = overview.Single(u => u.Client?.Name == "alice");
        var offline = overview.Single(u => u.Name == "offline:клиент");
        var pending = overview.Single(u => u.Pending != null);
        Check(online.State == "Подключён" && online.Address == "[2001:db8::1]:1984" &&
            online.Whitelist == "Актуален" && online.Ipv6BlockText == "Вкл" && online.LastActivity != null,
            "Live state did not reach the overview");
        Check(offline.State == "Не подключён" && offline.Address == "[::1]:1984" && offline.LastActivity == null && offline.Whitelist == "—",
            "Offline user displayed invented policy or activity");
        Check(pending.State == "Ожидает регистрации" && pending.CreatedAt == request.CreatedAt && pending.Client == null,
            "Pending registration was merged with a registered user of the same name");
        online.IsSelected = true;
        pending.IsSelected = true;
        Check(connected[0].IsSelected && request.IsSelected, "Selection was not shared with the existing views");
        Check(UserTableView.SelectedOrCurrent(registered, offline.Client, true).SequenceEqual(new[] { online.Client }),
            "Mixed selection targeted the wrong registered users");
        online.IsSelected = false;
        Check(UserTableView.SelectedOrCurrent(registered, offline.Client, true).Count == 0,
            "Pending-only selection unexpectedly targeted the context client");
        pending.IsSelected = false;
        Check(UserTableView.SelectedOrCurrent(registered, offline.Client).Single() == offline.Client,
            "Unselected offline user could not be used for token actions");

        online.IsSelected = true;
        var events = new List<NotifyCollectionChangedAction>();
        overview.CollectionChanged += (_, e) => events.Add(e.Action);
        UserTableView.UpdateRegistered(registered, connected, Registry(), [Live()]);
        UserTableView.UpdateOverview(overview, registered, connected, [request]);
        Check(events.Count == 0 && ReferenceEquals(overview[0], online) && online.IsSelected,
            "Refresh rebuilt overview rows or cleared selection");
        var properties = new List<string?>();
        online.PropertyChanged += (_, e) => properties.Add(e.PropertyName);
        UserTableView.UpdateRegistered(registered, connected, Registry(), []);
        UserTableView.UpdateOverview(overview, registered, connected, [request]);
        Check(online.State == "Не подключён" && online.LastActivity == null && online.Client!.Policy == null &&
            properties.Contains("State") && online.IsSelected && events.Count == 0,
            "Disconnect removed a registered user, retained stale policy or did not update the row");
        UserTableView.UpdateRegistered(registered, connected, Registry(), [Live()]);
        UserTableView.UpdateOverview(overview, registered, connected, [request]);
        Check(online.IsOnline && ReferenceEquals(online.Client, connected.Single()) && online.IsSelected,
            "Reconnect replaced the user or lost its selection");
        properties.Clear();
        UserTableView.UpdateRegistered(registered, connected, Registry().Skip(1), []);
        UserTableView.UpdateOverview(overview, registered, connected, [request]);
        properties.Clear();
        online.Client!.Address = "removed";
        Check(overview.Count == 2 && !overview.Contains(online) && properties.Count == 0 &&
            events.All(e => e != NotifyCollectionChangedAction.Reset), "Deleting a user retained its row or notification subscription");
        UserTableView.UpdateRegistered(registered, connected, Registry(), []);
        UserTableView.UpdateOverview(overview, registered, connected, []);
        Check(overview.Count == 2 && overview.All(u => u.Pending == null) && overview.Any(u => u.Name == "alice"),
            "Approval did not replace the pending entry with a registered user");
        foreach (var row in overview) row.Dispose();
    }

    private static void Check(bool condition, string message)
    {
        if (!condition) throw new Exception(message);
    }
}
