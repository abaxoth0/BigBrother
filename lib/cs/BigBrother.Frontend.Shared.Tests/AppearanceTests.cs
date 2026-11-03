using System.Text.Json;
using frontend.Services;

static class AppearanceTests
{
    public static void Run()
    {
        var directory = Path.Combine(Path.GetTempPath(), "bigbrother-appearance-" + Guid.NewGuid());
        Directory.CreateDirectory(directory);
        try
        {
            var path = Path.Combine(directory, "client", "appearance.json");
            CheckSizes(AppearancePreferences.Load(path), 14, 14, "Missing settings use both defaults");
            foreach (int text in new[] { 10, 14, 16, 20, 28, int.MinValue, int.MaxValue })
            foreach (int ui in new[] { 10, 14, 20, 28, int.MinValue, int.MaxValue })
            {
                new AppearancePreferences { TextFontSize = text, InterfaceFontSize = ui }.Save(path);
                CheckSizes(AppearancePreferences.Load(path), Math.Clamp(text, 10, 28), Math.Clamp(ui, 10, 28),
                    "Both sizes must persist independently within their bounds");
                Check(Directory.GetFiles(Path.GetDirectoryName(path)!).Length == 1, "No temporary files remain");
            }
            var preferences = AppearancePreferences.Load(path);
            preferences.TextFontSize = 16;
            Check(preferences.InterfaceFontSize == 28, "Changing text size must preserve interface size");
            preferences.InterfaceFontSize = 20;
            Check(preferences.TextFontSize == 16, "Changing interface size must preserve text size");
            preferences.Save(path);
            var serverPath = Path.Combine(directory, "server", "appearance.json");
            new AppearancePreferences { TextFontSize = 22, InterfaceFontSize = 12 }.Save(serverPath);
            CheckSizes(AppearancePreferences.Load(path), 16, 20, "Client preferences survive a server save");
            CheckSizes(AppearancePreferences.Load(serverPath), 22, 12, "Server preferences stay independent");
            foreach (string json in new[] { "{", "null", "[]", "{\"TextFontSize\":\"large\"}",
                         "{\"InterfaceFontSize\":\"large\"}", "{}" })
            {
                File.WriteAllText(path, json);
                CheckSizes(AppearancePreferences.Load(path), 14, 14, "Malformed settings fall back to defaults");
            }
            foreach (var (json, text, ui) in new[]
            {
                ("{\"BaseFontSize\":20}", 20, 20),
                ("{\"BaseFontSize\":999}", 28, 28),
                ("{\"BaseFontSize\":1}", 10, 10),
                ("{\"TextFontSize\":16,\"BaseFontSize\":20}", 16, 20),
                ("{\"BaseFontSize\":20,\"InterfaceFontSize\":14}", 20, 14),
                ("{\"InterfaceFontSize\":10,\"BaseFontSize\":22,\"TextFontSize\":28}", 28, 10),
                ("{\"TextFontSize\":18,\"FutureStyle\":true}", 18, 14),
                ("{\"InterfaceFontSize\":16}", 14, 16),
                ("{\"TextFontSize\":999,\"InterfaceFontSize\":1}", 28, 10),
                ("{\"BaseFontSize\":\"large\",\"TextFontSize\":18}", 18, 14)
            })
            {
                File.WriteAllText(path, json);
                var loaded = AppearancePreferences.Load(path);
                CheckSizes(loaded, text, ui, "Legacy migration and explicit settings precedence: " + json);
                loaded.Save(path);
                CheckSizes(AppearancePreferences.Load(path), text, ui, "Migration must survive a second load");
                using var saved = JsonDocument.Parse(File.ReadAllText(path));
                Check(!saved.RootElement.TryGetProperty("BaseFontSize", out _), "Saves must use only the split sizes");
            }
            Console.WriteLine("PASS: independent text/interface sizes, bounds, legacy migration, corrupt settings and frontend persistence");
        }
        finally { Directory.Delete(directory, true); }
    }

    private static void CheckSizes(AppearancePreferences preferences, int text, int ui, string message) =>
        Check(preferences.TextFontSize == text && preferences.InterfaceFontSize == ui, message);

    private static void Check(bool condition, string message)
    {
        if (!condition) throw new Exception(message);
    }
}
