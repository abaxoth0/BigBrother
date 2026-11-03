namespace frontend.Models;

public class WhitelistPreset
{
    public string Name { get; set; } = "";
    public List<string> ActiveWhitelists { get; set; } = new();
    public string WhitelistCountText => $"Активных списков: {ActiveWhitelists.Count}";
    public string Preview => ActiveWhitelists.Count == 0
        ? "Нет активных списков"
        : string.Join("\n", ActiveWhitelists.Select(name => $"• {name}"));
}
