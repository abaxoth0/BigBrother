using System.IO;
using System.Text.Json;

namespace frontend.Services;

public sealed class AppearancePreferences
{
    public const int DefaultFontSize = 14;
    public const int MinimumFontSize = 10;
    public const int MaximumFontSize = 28;

    private int _baseFontSize = DefaultFontSize;
    public int BaseFontSize
    {
        get => _baseFontSize;
        set => _baseFontSize = Math.Clamp(value, MinimumFontSize, MaximumFontSize);
    }

    public static AppearancePreferences Load(string path)
    {
        try
        {
            var preferences = JsonSerializer.Deserialize<AppearancePreferences>(File.ReadAllText(path)) ?? new();
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
