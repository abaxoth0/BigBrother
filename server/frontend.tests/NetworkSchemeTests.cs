using frontend.Models;
using frontend.Services;
using frontend.ViewModels;

internal static class NetworkSchemeTests
{
    public static void Run()
    {
        var directory = Path.Combine(Path.GetTempPath(), "bb-network-" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(directory);
        var path = Path.Combine(directory, "layout.json");
        try
        {
            var store = new NetworkLayoutStore(path);
            var map = new NetworkSchemeViewModel(store);
            map.SetServer("Офис", "10.0.0.1, 2001:db8::1");
            map.SetAvailable(true);
            var alice = new UserOverview(new ConnectedClient { Name = "Alice", Address = "10.0.0.2" });
            var offline = new UserOverview(new ConnectedClient { Name = "Offline", Address = "[2001:db8::2]:1984" });
            var pending = new UserOverview(new PendingRegistration { Name = "Alice", Address = "10.0.0.3" });
            alice.SetOnline(true);
            map.Refresh([alice, offline, pending], true);
            Check(map.Nodes.Count == 4 && map.Clients.Count == 3 && map.Server.Name == "Офис" && map.Server.IsOnline,
                "Map must include the server, connected, offline and pending clients");
            var node = map.Clients.Single(n => n.Key == "client:Alice");
            var request = map.Clients.Single(n => n.IsPending);
            Check(node.IsOnline && request.Key != node.Key && map.Clients.Single(n => n.Name == "Offline").Status == "Не подключён",
                "Offline clients or pending requests sharing a name must remain distinct");
            Check(map.Nodes.Select(n => (n.X, n.Y)).Distinct().Count() == 4, "Default positions must not overlap");
            map.Move(node, 1350, 800);
            map.Move(map.Server, 400, 500);
            map.Move(request, 2000, 1000);
            map.SaveLayout();
            Check(File.Exists(path) && map.CanvasWidth >= 2330 && map.CanvasHeight >= 1230,
                "Dragging must save the layout and extend the scrollable area");
            Check(Directory.GetFiles(directory).Length == 1, "Atomic saving left temporary files behind");
            var changes = new List<string?>();
            node.PropertyChanged += (_, e) => changes.Add(e.PropertyName);
            alice.Client!.Address = "10.0.0.22";
            alice.SetOnline(false);
            map.Refresh([alice, offline, pending], true);
            Check(ReferenceEquals(node, map.Clients[0]) && node.X == 1350 && node.Y == 800 &&
                node.Address == "10.0.0.22" && !node.IsOnline && changes.Contains(nameof(NetworkNode.Status)),
                "Status and address refreshes must update existing nodes without changing positions");
            map.SetAvailable(false);
            Check(map.Nodes.Count == 4 && map.Clients.All(n => !n.StatusKnown && !n.IsOnline) &&
                !map.Server.IsOnline && map.Server.Status != "Запущен", "Backend loss must retain nodes and mark status unknown");
            map.SetAvailable(true);
            map.Refresh([pending]);
            Check(!node.StatusKnown && map.Clients.Contains(node),
                "Pending refresh must retain registered nodes without restoring stale client status");
            alice.SetOnline(true);
            map.Refresh([alice, offline, pending], true);
            Check(node.IsOnline && node.StatusKnown && map.Summary.Contains("Подключено: 1"), "Reconnect must restore live status");
            map.MarkClientsUnknown();
            Check(map.Clients.All(n => !n.StatusKnown) && map.Server.IsOnline, "Failed client refresh must not display stale statuses");

            var restored = new NetworkSchemeViewModel(store);
            restored.SetAvailable(true);
            restored.Refresh([offline, alice, pending], true);
            var restoredAlice = restored.Clients.Single(n => n.Key == node.Key);
            Check(restoredAlice.X == 1350 && restoredAlice.Y == 800 && restored.Server.X == 400 && restored.Server.Y == 500,
                "Restart or changed client order lost saved positions");
            restored.Move(restoredAlice, -100, -1);
            Check(restoredAlice.X == 0 && restoredAlice.Y == 0, "Nodes must stay inside the canvas");
            restored.Move(restoredAlice, double.NaN, 15);
            Check(restoredAlice.X == 0 && restoredAlice.Y == 0, "Invalid drag coordinates were accepted");
            restored.SetNodeScale(2);
            Check(restoredAlice.Width == 500 && restoredAlice.CenterX == 250 && restoredAlice.Height == 300,
                "Font scaling must resize nodes and update connection endpoints");
            restored.ResetLayout();
            Check(restored.Nodes.Select(n => (n.X, n.Y)).Distinct().Count() == 4 &&
                store.Load()["client:Alice"].X == restoredAlice.X, "Reset must arrange nodes and persist positions");
            restored.Refresh([offline], true);
            Check(restored.Nodes.Count == 2, "Removed users remained on the map");
            restored.Refresh([offline, alice], true);
            Check(restored.Clients.Single(n => n.Key == node.Key).X == restoredAlice.X,
                "A returning registered client lost its position");

            var approved = new UserOverview(new ConnectedClient { Name = "New", Address = "10.0.0.9" });
            var waiting = new UserOverview(new PendingRegistration { Name = "New", Address = "10.0.0.9" });
            restored.Refresh([waiting], true);
            restored.Move(restored.Clients.Single(), 321, 654);
            restored.Refresh([approved], true);
            Check(restored.Clients.Single().X == 321 && restored.Clients.Single().Y == 654,
                "Approving a new client must preserve its pending node position");

            File.WriteAllText(path, "{broken");
            Check(new NetworkSchemeViewModel(store).Nodes.Count == 1, "Corrupted layout must fall back without blocking startup");
            File.WriteAllText(path, """{"server":{"X":-1,"Y":1},"null":null,"valid":{"X":3,"Y":4},"huge":{"X":100001,"Y":1}}""");
            Check(store.Load().Count == 1 && store.Load()["valid"].X == 3, "Invalid stored positions were accepted");
            File.WriteAllText(path, "null");
            Check(store.Load().Count == 0, "Null layout did not fall back safely");
            var failed = new NetworkSchemeViewModel(new NetworkLayoutStore(directory));
            failed.SaveLayout();
            Check(failed.SaveStatus.StartsWith("Не удалось"), "Save failures must be visible without crashing");
            foreach (var user in new[] { alice, offline, pending, approved, waiting }) user.Dispose();
            Console.WriteLine("PASS: network map identity, status refresh, movement, layout persistence, scaling and recovery");
        }
        finally { Directory.Delete(directory, recursive: true); }
    }

    private static void Check(bool condition, string message)
    {
        if (!condition) throw new Exception(message);
    }
}
