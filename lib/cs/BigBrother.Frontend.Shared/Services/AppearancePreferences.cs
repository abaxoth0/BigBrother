using System.IO;
using System.Text.Json;

namespace frontend.Services;

public sealed class AppearancePreferences
{
    public const int DefaultFontSize = 14;
    public const int MinimumFontSize = 10;
    public const int MaximumFontSize = 28;

    private int _textFontSize = DefaultFontSize;
    private int _interfaceFontSize = DefaultFontSize;

    public int TextFontSize
    {
        get => _textFontSize;
        set => _textFontSize = Math.Clamp(value, MinimumFontSize, MaximumFontSize);
    }

    public int InterfaceFontSize
    {
        get => _interfaceFontSize;
        set => _interfaceFontSize = Math.Clamp(value, MinimumFontSize, MaximumFontSize);
    }

    public static AppearancePreferences Load(string path)
    {
        try
        {
            using var document = JsonDocument.Parse(File.ReadAllText(path));
            var preferences = document.Deserialize<AppearancePreferences>() ?? new();
            var root = document.RootElement;
            if (root.ValueKind == JsonValueKind.Object && root.TryGetProperty("BaseFontSize", out var legacy) &&
                legacy.ValueKind == JsonValueKind.Number && legacy.TryGetInt32(out int size))
            {
                if (!root.TryGetProperty(nameof(TextFontSize), out _)) preferences.TextFontSize = size;
                if (!root.TryGetProperty(nameof(InterfaceFontSize), out _)) preferences.InterfaceFontSize = size;
            }
            return preferences;
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException or JsonException)
        {
            return new();
        }
    }

    public void Save(string path)
    {
        Directory.CreateDirectory(Path.GetDirectoryName(Path.GetFullPath(path))!);
        var temporary = path + "." + Guid.NewGuid().ToString("N") + ".tmp";
        try
        {
            File.WriteAllText(temporary, JsonSerializer.Serialize(this));
            File.Move(temporary, path, overwrite: true);
        }
        finally
        {
            if (File.Exists(temporary)) File.Delete(temporary);
        }
    }
}
