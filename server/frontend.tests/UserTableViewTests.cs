using System.Collections.ObjectModel;
using System.Collections.Specialized;
using frontend.Models;
using frontend.ViewModels;

internal static class UserTableViewTests
{
    public static void Run()
    {
        var client = new ConnectedClient { Name = "selected", Address = "10.0.0.1" };
        Verify(client, new ConnectedClient { Name = "other" }, new ConnectedClient { Name = "new" }, () =>
        {
            client.LastActivity = DateTime.Now;
            client.Policy = new ClientPolicyStatus { FiltrationEnabled = true };
            client.WhitelistCurrent = true;
        });
        var pending = new PendingRegistration { Name = "selected", Address = "10.0.0.2" };
        Verify(pending, new PendingRegistration { Name = "other" }, new PendingRegistration { Name = "new" }, () =>
        {
            pending.Address = "10.0.0.3";
            pending.CreatedAt = DateTime.Now;
        });
        VerifySelection(Enumerable.Range(0, 5).Select(i => new ConnectedClient { Name = i.ToString() }).ToList());
        VerifySelection(Enumerable.Range(0, 5).Select(i => new PendingRegistration { Name = i.ToString() }).ToList());
    }

    private static void Verify<T>(T selected, T other, T added, Action changeFields) where T : class, ISelectableRow
    {
        var rows = new ObservableCollection<T> { other, selected };
        var events = new List<NotifyCollectionChangedEventArgs>();
        rows.CollectionChanged += (_, e) => events.Add(e);
        selected.IsSelected = true;
        changeFields();
        UserTableView.Update(rows, new[] { other, selected });
        Check(events.Count == 0, "Refreshing existing users changed the bound collection");
        Check(ReferenceEquals(rows[1], selected), "Refresh replaced the selected row");
        Check(selected.IsSelected, "Refresh cleared the selected checkbox");

        UserTableView.Update(rows, new[] { other, selected, added });
        Check(events.Count == 1 && events[0].Action == NotifyCollectionChangedAction.Add &&
            ReferenceEquals(events[0].NewItems![0], added), "Adding a user disturbed existing rows");
        events.Clear();
        UserTableView.Update(rows, new[] { selected, added });
        Check(events.Count == 1 && events[0].Action == NotifyCollectionChangedAction.Remove &&
            ReferenceEquals(events[0].OldItems![0], other), "Removing another user disturbed the selected row");

        events.Clear();
        UserTableView.Update(rows, new[] { other, selected, added });
        Check(events.Count == 1 && events[0].Action == NotifyCollectionChangedAction.Add &&
            ReferenceEquals(rows[1], selected), "Restoring a filtered row disturbed a retained row");
        events.Clear();
        UserTableView.Update(rows, new[] { selected, other, added });
        Check(events.Count == 1 && events[0].Action == NotifyCollectionChangedAction.Move &&
            rows.SequenceEqual(new[] { selected, other, added }), "Reordering replaced rows or lost their order");
        events.Clear();
        UserTableView.Update(rows, Array.Empty<T>());
        Check(rows.Count == 0 && events.All(e => e.Action == NotifyCollectionChangedAction.Remove),
            "Filtering out all users reset the collection");
    }

    private static void VerifySelection<T>(List<T> rows) where T : class, ISelectableRow
    {
        var selection = new CheckboxSelection<T>();
        var changed = new List<string?>();
        rows[1].PropertyChanged += (_, e) => changed.Add(e.PropertyName);
        Check(!selection.HandleClick(rows, rows[1], false, false) && rows.All(r => !r.IsSelected),
            "Plain row click changed checkboxes");
        selection.HandleClick(rows, rows[1], true, false);
        Check(rows[1].IsSelected && changed.SequenceEqual(new[] { "IsSelected" }),
            "Ctrl-click did not notify the checkbox and header");
        Check(UserTableView.SelectedOrCurrent(rows, rows[4]).SequenceEqual(new[] { rows[1] }),
            "Context row overrode the checked user");
        selection.HandleClick(rows, rows[3], false, true);
        Check(rows.Select(r => r.IsSelected).SequenceEqual(new[] { false, true, true, true, false }),
            "Shift-click did not select the complete range");
        selection.HandleClick(rows, rows[2], false, true);
        Check(rows.Select(r => r.IsSelected).SequenceEqual(new[] { false, true, true, false, false }),
            "Shift-click did not shrink the range from its anchor");
        var targets = UserTableView.SelectedOrCurrent(rows, rows[4]);
        foreach (var row in rows) row.IsSelected = false;
        Check(targets.SequenceEqual(new[] { rows[1], rows[2] }), "Clearing selection changed the action snapshot");
        Check(UserTableView.SelectedOrCurrent(rows, rows[4]).SequenceEqual(new[] { rows[4] }) &&
            UserTableView.SelectedOrCurrent(rows, null).Count == 0, "Single-row action fallback is incorrect");

        selection.HandleClick(rows, rows[0], true, false);
        selection.HandleClick(rows, rows[2], false, false);
        selection.HandleClick(rows, rows[4], true, true);
        Check(rows.Select(r => r.IsSelected).SequenceEqual(new[] { true, false, true, true, true }),
            "Ctrl+Shift-click lost existing checks outside the range");
        var sorted = rows.AsEnumerable().Reverse().ToList();
        selection.HandleClick(sorted, rows[4], false, false);
        selection.HandleClick(sorted, rows[2], false, true);
        Check(rows.Select(r => r.IsSelected).SequenceEqual(new[] { false, false, true, true, true }),
            "Range selection ignored the displayed sort order");
        var visible = new ObservableCollection<T>();
        UserTableView.Update(visible, rows.Take(2));
        Check(UserTableView.SelectedOrCurrent(rows, rows[0]).Count == 3,
            "Search removed checked users from batch actions");
        selection.HandleClick(rows, rows[0], false, false);
        var removed = rows[0];
        rows.RemoveAt(0);
        selection.HandleClick(rows, rows[2], false, true);
        Check(rows.Count(r => r.IsSelected) == 1 && rows[2].IsSelected,
            "Removing the anchor broke subsequent range selection");
        Check(!UserTableView.SelectedOrCurrent(rows, removed).Contains(removed),
            "Checked users were combined with a disconnected context row");
    }

    private static void Check(bool condition, string message)
    {
        if (!condition) throw new Exception(message);
    }
}
