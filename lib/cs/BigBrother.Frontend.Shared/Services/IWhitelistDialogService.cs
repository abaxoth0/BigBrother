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
    Task ShowDialogAsync(
        WhitelistDialogMode mode,
        Func<WhitelistDialogResult, Task<bool>> saveAsync,
        string? initialName = null,
        List<string>? initialEntries = null);
}
