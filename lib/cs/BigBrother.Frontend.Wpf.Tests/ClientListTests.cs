using System.Windows.Controls;
using frontend;
using frontend.ViewModels;

internal static partial class Program
{
    private static void TestClientListPages(MainWindow window)
    {
        var vm = (MainViewModel)window.DataContext;
        var tabs = SettingsVisuals<RadioButton>(window).Where(tab => tab.GroupName == "Nav").ToList();
        var firewall = tabs.Single(tab => Equals(tab.Content, "Firewall"));
        var apps = tabs.Single(tab => Equals(tab.Content, "Приложения"));
        Check(!apps.IsEnabled && !firewall.IsEnabled, "Offline list navigation must be disabled");

        var setConnected = typeof(MainViewModel).GetProperty(nameof(MainViewModel.BackendConnected))!.GetSetMethod(true)!;
        setConnected.Invoke(vm, new object[] { true });
        SettingsLayout(window);
        firewall.SetCurrentValue(RadioButton.IsCheckedProperty, true);
        SettingsLayout(window);
        Check(vm.SelectedSection == NavigationSection.Firewall, "Firewall must open the whitelist page");
        apps.SetCurrentValue(RadioButton.IsCheckedProperty, true);
        SettingsLayout(window);

        var label = (TextBlock)window.FindName("ProcessBlacklistLabel");
        var text = SettingsVisuals<TextBox>(window).Single(box => box.IsVisible && box.IsReadOnly);
        Check(vm.SelectedSection == NavigationSection.Applications && label.IsVisible,
            "Applications must show the read-only blacklist");
        var list = vm.ProcessBlacklist;
        list.RefreshAsync(() => Task.FromResult<List<string>?>(["[3]game*.exe", "!редактор.exe"])).GetAwaiter().GetResult();
        SettingsLayout(window);
        Check(text.Text == "[3]game*.exe" + Environment.NewLine + "!редактор.exe" && list.Message == "",
            "The client must show every executable rule, priority and exception");
        list.RefreshAsync(() => Task.FromResult<List<string>?>([])).GetAwaiter().GetResult();
        Check(text.Text == "" && list.Message == "Чёрный список пуст.", "A successful empty list must appear empty");
        list.RefreshAsync(() => Task.FromResult<List<string>?>(null)).GetAwaiter().GetResult();
        Check(list.Message == "Чёрный список недоступен.", "A failed request must appear unavailable");

        vm.ServerConnected = true;
        var pending = new TaskCompletionSource<List<string>?>();
        var previous = list.RefreshAsync(() => pending.Task);
        vm.ServerConnected = false;
        pending.SetResult(["stale.exe"]);
        PumpUntil(() => previous.IsCompletedSuccessfully, "Delayed list fetch stalled", busyRendering: false);
        Check(text.Text == "" && list.Message == "Чёрный список недоступен.",
            "Rules received after a disconnect must stay hidden");
        setConnected.Invoke(vm, new object[] { false });
        SettingsLayout(window);
        Check(vm.SelectedSection == NavigationSection.Main && !apps.IsEnabled && !text.IsVisible,
            "Backend loss must close the applications page");
    }
}
