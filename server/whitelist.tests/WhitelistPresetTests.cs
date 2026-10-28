using frontend.Models;
using frontend.ViewModels;

static class WhitelistPresetTests
{
    private static void Check(bool condition, string message)
    {
        if (!condition) throw new Exception(message);
    }

    public static async Task RunAsync()
    {
        var large = new WhitelistPreset { Name = "Работа", ActiveWhitelists = Enumerable.Range(0, 150).Select(i => $"WL {i:000}").ToList() };
        var menu = new WhitelistPresetMenuViewModel();
        menu.BeginLoading();
        Check(menu.Status == "Загрузка…", "Preset menu lost loading status");
        menu.SearchText = "  рАбОтА ";
        menu.SetPresets(Enumerable.Range(0, 150).Select(i => new WhitelistPreset { Name = $"Preset {i:000}" }).Append(large));
        Check(menu.VisiblePresets.SequenceEqual(new[] { large }) && menu.Status == "", "Preset name search failed with many presets");
        menu.RemovePreset(large.Name);
        Check(menu.SearchText == "  рАбОтА " && menu.VisiblePresets.Count == 0 && menu.Status == "Пресеты не найдены.",
            "Deleting a preset reset the search or left a stale visible entry");
        menu.SearchText = "";
        Check(menu.VisiblePresets.Count == 150, "Deleting a preset removed unrelated presets");
        menu.SetPresets(menu.VisiblePresets.Append(large));
        menu.SearchText = "WL";
        Check(menu.VisiblePresets.Count == 0 && menu.Status == "Пресеты не найдены.", "Preset search incorrectly searched whitelist names");
        menu.SearchText = "";
        Check(menu.VisiblePresets.Count == 151 && large.WhitelistCountText == "Активных списков: 150", "Menu lost compact whitelist counts or failed to clear search");
        menu.SetError("offline");
        Check(menu.Status == "offline", "Menu hid a loading failure");
        menu.SetPresets(Array.Empty<WhitelistPreset>());
        Check(menu.Status.Contains("Пресетов пока нет"), "Empty menu retained an old error");

        var requests = new List<string>();
        var pending = new Dictionary<string, TaskCompletionSource<List<string>>>();
        Task<List<string>> Load(string name)
        {
            requests.Add(name);
            var request = new TaskCompletionSource<List<string>>();
            pending[name] = request;
            return request.Task;
        }
        var preview = new WhitelistPresetPreviewViewModel(large, Load);
        var firstLoad = preview.RulesLoadTask;
        Check(requests.SequenceEqual(new[] { "WL 000" }) && preview.VisibleWhitelists.Count == 150,
            "Preview eagerly fetched every whitelist instead of the selected list");
        preview.SelectedWhitelist = "WL 100";
        var latestLoad = preview.RulesLoadTask;
        pending["WL 100"].SetResult(new() { "# comment", "[10]", "example.com:443", "!blocked.example.com" });
        await latestLoad;
        pending["WL 000"].SetResult(new() { "stale.org" });
        await firstLoad;
        Check(preview.RulesText == "# comment\n[10]\nexample.com:443\n!blocked.example.com" && preview.RulesStatus == "",
            "Preview dropped rules/comments/priorities or let an earlier request overwrite the selected list");
        preview.SearchText = "wl 14";
        Check(preview.VisibleWhitelists.Count == 10 && preview.SelectedWhitelist == null && preview.RulesText == "",
            "Whitelist search retained rules for a hidden selection");
        preview.SelectedWhitelist = "WL 140";
        pending["WL 140"].SetException(new IOException("offline"));
        await preview.RulesLoadTask;
        Check(preview.RulesStatus.Contains("offline") && preview.RulesText == "", "Preview presented a failed read as an empty whitelist");
        preview.RefreshRulesCommand.Execute(null);
        pending["WL 140"].SetResult(new());
        await preview.RulesLoadTask;
        Check(preview.RulesStatus == "Список пуст.", "Retry did not recover to an empty whitelist");
        preview.SelectedWhitelist = "WL 141";
        var closingLoad = preview.RulesLoadTask;
        preview.Close();
        pending["WL 141"].SetResult(new() { "late.org" });
        await closingLoad;
        Check(preview.RulesText == "", "Closing the preview allowed a late response to update it");

        var empty = new WhitelistPresetPreviewViewModel(new WhitelistPreset(), _ => throw new Exception("Empty preset requested rules"));
        Check(empty.ListStatus.Contains("нет активных") && !empty.RefreshRulesCommand.CanExecute(null), "Empty preset cannot be previewed");
    }
}
