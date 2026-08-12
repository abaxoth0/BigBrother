namespace frontend.Services;

public enum WhitelistDialogMode
{
    Create,
    Edit
}

public class WhitelistDialogResult
{
    public string Name { get; set; } = "";
    public List<string> Entries { get; set; } = new();
}

public interface IWhitelistDialogService
{
    /// <summary>Shows the whitelist edit dialog. Returns null if cancelled.</summary>
    Task<WhitelistDialogResult?> ShowDialogAsync(
        WhitelistDialogMode mode,
        string? initialName = null,
        List<string>? initialEntries = null);
}
