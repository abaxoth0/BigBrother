using System.IO;
using System.Text.Json;

namespace frontend.Services;

public sealed record NetworkPosition(double X, double Y)
{
    public bool IsValid => double.IsFinite(X) && double.IsFinite(Y) && X is >= -100000 and <= 100000 && Y is >= -100000 and <= 100000;
}

public sealed class NetworkLayoutStore(string path)
{
    public Dictionary<string, NetworkPosition> Load()
    {
        try
        {
            var positions = JsonSerializer.Deserialize<Dictionary<string, NetworkPosition?>>(File.ReadAllText(path));
            return positions?.Where(p => p.Value?.IsValid == true).ToDictionary(p => p.Key, p => p.Value!) ?? new();
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException or JsonException)
        {
            return new();
        }
    }

    public void Save(Dictionary<string, NetworkPosition> positions)
    {
        Directory.CreateDirectory(Path.GetDirectoryName(Path.GetFullPath(path))!);
        var temporary = path + "." + Guid.NewGuid().ToString("N") + ".tmp";
        try
        {
            File.WriteAllText(temporary, JsonSerializer.Serialize(positions));
            File.Move(temporary, path, overwrite: true);
        }
        finally
        {
            if (File.Exists(temporary)) File.Delete(temporary);
        }
    }
}
