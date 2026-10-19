using frontend.Services;
using System.Text.Json;

await ManualUpdateTests.RunAsync();
await NetworkShareTests.RunAsync();

static void Assert(bool condition, string message)
{
    if (!condition) throw new Exception(message);
}

static void Reject(Action action)
{
    try { action(); } catch (IOException) { return; }
    throw new Exception("Unsafe operation was accepted.");
}

var root = Path.Combine(Path.GetTempPath(), "bigbrother-update-tests-" + Guid.NewGuid().ToString("N"));
Directory.CreateDirectory(root);
try
{
    var install = Path.Combine(root, "installed");
    var backup = Path.Combine(root, "backup");
    Directory.CreateDirectory(Path.Combine(install, "GUI"));
    Directory.CreateDirectory(Path.Combine(install, "config"));
    File.WriteAllText(Path.Combine(install, "GUI", "client.exe"), "old program");
    File.WriteAllText(Path.Combine(install, "config", "config.ini"), "old credentials");
    File.WriteAllText(Path.Combine(install, "snapshot.json"), "an unrelated user file");
    var captured = new List<string>();
    var snapshot = UpdateSnapshot.Capture(install, backup, (path, _) => {
        captured.Add(path);
        return "saved ACL for " + Path.GetFileName(path);
    });
    var journal = Path.Combine(root, "transaction.json");
    UpdateSnapshot.WriteJson(journal, snapshot);
    snapshot = JsonSerializer.Deserialize<UpdateSnapshot>(File.ReadAllText(journal))!;

    File.WriteAllText(Path.Combine(install, "GUI", "client.exe"), "partial new program");
    File.WriteAllText(Path.Combine(install, "config", "config.ini"), "migrated credentials");
    Directory.CreateDirectory(Path.Combine(install, "new-data", "nested"));
    File.WriteAllText(Path.Combine(install, "new-data", "nested", "new-file"), "new");
    File.Delete(Path.Combine(install, "snapshot.json"));
    var restored = new List<string>();
    snapshot.Restore(install, backup, (path, _, acl) => {
        var name = Path.GetFileName(path).Split(".bbupdate-")[0];
        Assert(acl == "saved ACL for " + name, "Lost file security metadata.");
        if (!path.Contains(".bbupdate-")) restored.Add(path);
    });
    Assert(File.ReadAllText(Path.Combine(install, "GUI", "client.exe")) == "old program", "Program was not restored.");
    Assert(File.ReadAllText(Path.Combine(install, "config", "config.ini")) == "old credentials", "Configuration was not restored.");
    Assert(File.ReadAllText(Path.Combine(install, "snapshot.json")) == "an unrelated user file", "Backup metadata collided with a user file.");
    Assert(!Directory.Exists(Path.Combine(install, "new-data")), "Added files were not removed.");
    Assert(restored.Count == captured.Count, "Some security descriptors were not restored.");

    snapshot.Restore(install, backup, (_, _, _) => {});
    Assert(File.ReadAllText(Path.Combine(install, "GUI", "client.exe")) == "old program", "Repeated recovery changed files.");
    snapshot.Restore(install, backup, (_, _, _) => {},
        _ => throw new IOException("Unchanged files must not be rewritten, including loaded drivers."));

    var newProgram = Path.Combine(root, "new-program");
    File.WriteAllText(newProgram, "new program");
    Reject(() => UpdateSnapshot.Replace(newProgram, Path.Combine(install, "GUI", "client.exe"), null,
        _ => throw new IOException("Injected ACL restoration failure.")));
    Assert(File.ReadAllText(Path.Combine(install, "GUI", "client.exe")) == "old program", "A replacement was published before its permissions were restored.");
    Assert(!Directory.EnumerateFiles(Path.Combine(install, "GUI")).Any(f => f.Contains(".bbupdate-")), "Failed replacement left temporary data behind.");

    File.WriteAllText(Path.Combine(install, "GUI", "client.exe"), "failed version");
    var restoredFiles = 0;
    Reject(() => snapshot.Restore(install, backup, (_, _, _) => {
        if (++restoredFiles == 2) throw new IOException("Injected recovery interruption.");
    }));
    snapshot.Restore(install, backup, (_, _, _) => {});
    Assert(File.ReadAllText(Path.Combine(install, "GUI", "client.exe")) == "old program", "Interrupted recovery could not be resumed.");

    File.WriteAllText(Path.Combine(backup, "GUI", "client.exe"), "damaged backup");
    File.WriteAllText(Path.Combine(install, "GUI", "client.exe"), "leave this untouched");
    Reject(() => snapshot.Restore(install, backup, (_, _, _) => {}));
    Assert(File.ReadAllText(Path.Combine(install, "GUI", "client.exe")) == "leave this untouched", "Recovery mutated files before verifying the entire backup.");

    Reject(() => UpdateSnapshot.SafePath(install, "../outside"));
    Reject(() => UpdateSnapshot.SafePath(install, "file:alternate-stream"));
    if (!OperatingSystem.IsWindows())
    {
        Directory.CreateSymbolicLink(Path.Combine(install, "linked"), backup);
        Reject(() => UpdateSnapshot.SafePath(install, "linked/client.exe"));
        Reject(() => UpdateSnapshot.Capture(install, Path.Combine(root, "other-backup"), (_, _) => ""));
        Directory.Delete(Path.Combine(install, "linked"));
    }
    Console.WriteLine("PASS: complete snapshots, durable journal, rollback after partial replacement, config recovery, repeated recovery, damaged-backup rejection and unsafe paths.");
}
finally { Directory.Delete(root, true); }
