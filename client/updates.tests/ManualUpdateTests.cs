using frontend.Services;
using frontend.ViewModels;
using System.Text.Json;

public static class ManualUpdateTests
{
    private static void Assert(bool condition, string message)
    {
        if (!condition) throw new Exception(message);
    }

    public static async Task RunAsync()
    {
        var expected = File.ReadAllText(Path.Combine(AppContext.BaseDirectory, "expected-version.txt")).Trim();
        Assert(frontend.BuildVersion.Current == expected, "The frontend informational version does not match VERSION.");
        var assembly = System.Reflection.Assembly.GetExecutingAssembly();
        Assert(assembly.GetName().Version!.ToString() == expected + ".0", "The assembly version does not match VERSION.");
        Assert(System.Reflection.CustomAttributeExtensions.GetCustomAttribute<System.Reflection.AssemblyFileVersionAttribute>(assembly)!.Version == expected + ".0", "The Windows file version does not match VERSION.");
        var update = new ClientUpdate("1.4.0", "update.exe", @"Z:\shared\update.exe", 4096, new string('a', 64),
            new ReleaseNotes("1.4.0", [new ReleaseNoteSection("fixed", [new string('я', 10000)])]));
        update.Validate();
        var decoded = JsonSerializer.Deserialize<ClientUpdate>(JsonSerializer.Serialize(update))!;
        Assert(decoded.ReleaseNotes!.Sections[0].Items[0].Length == 10000, "Long Unicode notes were lost.");
        Assert(decoded.ReleaseNotes.Sections[0].Title == "Исправлено", "Release sections are not localized.");
        Assert(update.IsNewerThan("1.3.0") && !update.IsNewerThan("1.4.0.0"), "Versions were compared incorrectly.");
        Assert(!update.SamePackage(update with { Sha256 = new string('b', 64) }), "Changed packages were accepted as reviewed.");

        var checks = 0;
        var installs = 0;
        var pending = new TaskCompletionSource<ClientUpdate?>();
        var vm = new UpdatesViewModel("1.3.0", () => { checks++; return pending.Task; }, _ => { installs++; return Task.CompletedTask; });
        Assert(checks == 0 && installs == 0, "The view model performed an automatic update check.");
        Assert(!vm.CanInstall && !vm.InstallCommand.CanExecute(null), "Installation was available before checking.");
        var check = vm.CheckAsync();
        await vm.CheckAsync();
        Assert(checks == 1 && vm.Busy && !vm.CheckCommand.CanExecute(null) && !vm.CanInstall, "Duplicate checks were not blocked.");
        pending.SetResult(update);
        await check;
        Assert(installs == 0 && vm.CanInstall && vm.AvailableVersion == "1.4.0" && vm.Sections.Length == 1, "Checking installed the package or failed to expose its notes.");
        await vm.InstallAsync();
        Assert(installs == 1, "Explicit installation was not invoked.");

        var attempt = 0;
        vm = new UpdatesViewModel("1.3.0", () => ++attempt == 1 ? Task.FromResult<ClientUpdate?>(update)
            : Task.FromException<ClientUpdate?>(new IOException("Server unavailable")), _ => Task.CompletedTask);
        await vm.CheckAsync();
        await vm.CheckAsync();
        Assert(!vm.CanInstall && vm.Sections.Length == 0 && vm.Status.Contains("Server unavailable"), "A failed check retained a stale installable package.");

        vm = new UpdatesViewModel("1.3.0", () => Task.FromResult<ClientUpdate?>(null), _ => Task.CompletedTask);
        await vm.CheckAsync();
        Assert(!vm.CanInstall && !vm.Busy, "An empty publication enabled installation.");

        var legacy = update with { ReleaseNotes = null };
        vm = new UpdatesViewModel("1.3.0", () => Task.FromResult<ClientUpdate?>(legacy), _ => Task.FromException(new IOException("Package changed")));
        await vm.CheckAsync();
        Assert(vm.CanInstall && vm.NotesHint.Length > 0, "A legacy manifest without notes was rejected.");
        await vm.InstallAsync();
        Assert(!vm.CanInstall && vm.Status.Contains("Package changed"), "A failed install retained a stale package.");
        Console.WriteLine("PASS: explicit update checks, separate installation, release-note preview, old manifests, stale-state rejection and failure reporting.");
    }
}
