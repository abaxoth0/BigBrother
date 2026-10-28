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
            Check(AppearancePreferences.Load(path).BaseFontSize == 14, "Missing settings use the default size");
            foreach (int size in new[] { 10, 14, 16, 20, 28, int.MinValue, int.MaxValue })
            {
                new AppearancePreferences { BaseFontSize = size }.Save(path);
                Check(AppearancePreferences.Load(path).BaseFontSize == Math.Clamp(size, 10, 28),
                    "Saved font sizes survive restarts and stay within bounds");
                Check(Directory.GetFiles(Path.GetDirectoryName(path)!).Length == 1, "No temporary files remain");
            }
            var serverPath = Path.Combine(directory, "server", "appearance.json");
            new AppearancePreferences { BaseFontSize = 16 }.Save(serverPath);
            Check(AppearancePreferences.Load(path).BaseFontSize == 28 &&
                  AppearancePreferences.Load(serverPath).BaseFontSize == 16, "Frontend preferences stay independent");
            foreach (string json in new[] { "{", "null", "[]", "{\"BaseFontSize\":\"large\"}", "{}" })
            {
                File.WriteAllText(path, json);
                Check(AppearancePreferences.Load(path).BaseFontSize == 14, "Malformed settings fall back to the default");
            }
            File.WriteAllText(path, "{\"BaseFontSize\":20,\"FutureStyle\":true}");
            Check(AppearancePreferences.Load(path).BaseFontSize == 20, "Future style settings are tolerated");
            File.WriteAllText(path, "{\"BaseFontSize\":999}");
            Check(AppearancePreferences.Load(path).BaseFontSize == 28, "Loaded values are bounded");
            Console.WriteLine("PASS: appearance persistence, bounds, corrupt settings and separate frontend preferences");
        }
        finally { Directory.Delete(directory, true); }
    }

    private static void Check(bool condition, string message)
    {
        if (!condition) throw new Exception(message);
    }
}
