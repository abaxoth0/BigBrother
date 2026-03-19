namespace frontend.Models;

public enum WhitelistMatchType
{
    Exact,      // "google.com" - exact domain match
    Wildcard,   // "*.github.com" - domain and all subdomains
    Substring   // "\"microsoft\"" - any domain containing substring
}

public class WhitelistEntry
{
    public string Raw { get; set; } = "";
    public string Domain { get; set; } = "";
    public WhitelistMatchType MatchType { get; set; }

    public string GetPrettyDescription()
    {
        return MatchType switch
        {
            WhitelistMatchType.Exact => $"{Domain}",
            WhitelistMatchType.Wildcard => $"{Domain} и все его поддомены",
            WhitelistMatchType.Substring => $"Любой домен, содержащий: {Domain}",
            _ => Raw
        };
    }

    public static WhitelistEntry Parse(string raw)
    {
        var entry = new WhitelistEntry { Raw = raw };

        if (raw.StartsWith("*."))
        {
            entry.MatchType = WhitelistMatchType.Wildcard;
            entry.Domain = raw.Substring(2);
        }
        else if (raw.StartsWith("\"") && raw.EndsWith("\""))
        {
            entry.MatchType = WhitelistMatchType.Substring;
            entry.Domain = raw.Trim('"');
        }
        else
        {
            entry.MatchType = WhitelistMatchType.Exact;
            entry.Domain = raw;
        }

        return entry;
    }
}
