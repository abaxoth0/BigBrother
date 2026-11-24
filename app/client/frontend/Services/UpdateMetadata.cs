using System.IO;
using System.Text;
using System.Text.Json;
using System.Text.Json.Serialization;
using System.Text.RegularExpressions;

namespace frontend.Services;

public sealed record ReleaseNoteSection(
    [property: JsonPropertyName("kind")] string Kind,
    [property: JsonPropertyName("items")] string[] Items)
{
    public string Title => Kind switch {
        "added" => "Добавлено", "removed" => "Удалено", "changed" => "Изменено",
        "fixed" => "Исправлено", "deprecated" => "Устарело", "security" => "Безопасность", _ => Kind
    };
}

public sealed record ReleaseNotes(
    [property: JsonPropertyName("version")] string Version,
    [property: JsonPropertyName("sections")] ReleaseNoteSection[] Sections);

public sealed record ClientUpdate(
    [property: JsonPropertyName("version")] string Version,
    [property: JsonPropertyName("file")] string File,
    [property: JsonPropertyName("location")] string Location,
    [property: JsonPropertyName("size")] long Size,
    [property: JsonPropertyName("sha256")] string Sha256,
    [property: JsonPropertyName("release_notes")] ReleaseNotes? ReleaseNotes = null)
{
    public const int MaxMetadataSize = 128 * 1024;

    public void Validate()
    {
        if (string.IsNullOrEmpty(Version) || !Regex.IsMatch(Version, @"^[0-9]{1,5}\.[0-9]{1,5}\.[0-9]{1,5}(\.[0-9]{1,5})?$") ||
            !System.Version.TryParse(Version, out _) || Size < 2 || Size > 1024L * 1024 * 1024 ||
            string.IsNullOrEmpty(Sha256) || !Regex.IsMatch(Sha256, "^[a-f0-9]{64}$") ||
            string.IsNullOrEmpty(Location) || !Regex.IsMatch(Location, @"^(?:[a-zA-Z]:\\|\\\\)") ||
            Location.Length > 2048 || Location.IndexOfAny(['\r', '\n', '\0', '"']) >= 0 ||
            !Location.EndsWith(".exe", StringComparison.OrdinalIgnoreCase)) throw new IOException("Некорректные сведения об обновлении.");
        if (ReleaseNotes != null &&
            (string.IsNullOrWhiteSpace(ReleaseNotes.Version) || ReleaseNotes.Sections == null || ReleaseNotes.Sections.Length > 6 ||
             ReleaseNotes.Sections.Any(s => s == null || s.Kind is not ("added" or "removed" or "changed" or "fixed" or "deprecated" or "security") ||
                 s.Items == null || s.Items.Any(string.IsNullOrWhiteSpace))))
            throw new IOException("Некорректное описание изменений.");
        if (Encoding.UTF8.GetByteCount(JsonSerializer.Serialize(this, new JsonSerializerOptions {
                Encoder = System.Text.Encodings.Web.JavaScriptEncoder.UnsafeRelaxedJsonEscaping })) >= MaxMetadataSize)
            throw new IOException("Описание обновления слишком большое.");
    }

    public bool IsNewerThan(string installed)
    {
        static System.Version Normalize(System.Version version) => new(version.Major, version.Minor, Math.Max(0, version.Build), Math.Max(0, version.Revision));
        return System.Version.TryParse(installed, out var current) && Normalize(System.Version.Parse(Version)) > Normalize(current);
    }

    public bool SamePackage(ClientUpdate other) => Version == other.Version && Size == other.Size &&
        Sha256 == other.Sha256 && string.Equals(Location, other.Location, StringComparison.OrdinalIgnoreCase);
}
