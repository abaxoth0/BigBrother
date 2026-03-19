namespace frontend.Models;

public class StatusInfo
{
    public string DaemonStatus { get; set; } = "Unknown";
    public int DaemonPid { get; set; }
    public string ClientStatus { get; set; } = "Unknown";
    public int ClientPid { get; set; }
    public string HostName { get; set; } = "";
    public string IpAddress { get; set; } = "";
    public int WhitelistCount { get; set; }
    public DateTime LastUpdate { get; set; }
}
