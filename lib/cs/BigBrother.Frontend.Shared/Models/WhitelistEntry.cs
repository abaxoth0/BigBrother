namespace frontend.Models;

public enum WhitelistMatchType
{
    Exact,      // "google.com" - exact domain match
    Wildcard,   // "*.github.com" - domain and all subdomains
    Substring,  // "\"microsoft\"" - any domain containing substring
    Exception   // "!games.yandex.ru" - excluded from wildcard
}

public class WhitelistEntry
{
    public string Raw { get; set; } = "";
    public string Domain { get; set; } = "";
    public string Ports { get; set; } = "";
    public WhitelistMatchType MatchType { get; set; }

    public string GetPrettyDescription()
    {
        var description = MatchType switch
        {
            WhitelistMatchType.Exact => $"{Domain}",
            WhitelistMatchType.Wildcard => $"{Domain} и все его поддомены",
            WhitelistMatchType.Substring => $"Любой домен, содержащий: {Domain}",
            WhitelistMatchType.Exception => $"Кроме: {Domain}",
            _ => Raw
        };
        return Ports.Length == 0 ? description : $"{description} (порты: {Ports})";
    }

    public static WhitelistEntry Parse(string raw)
    {
        var entry = new WhitelistEntry { Raw = raw };
        var domain = raw;

        bool isException = raw.StartsWith('!');
        if (isException)
            domain = raw.Substring(1);

        int colon = domain.IndexOf(':');
        if (colon >= 0)
        {
            entry.Ports = domain[(colon + 1)..].Trim('{', '}');
            domain = domain[..colon];
        }

        if (domain.StartsWith("*."))
        {
            entry.MatchType = isException ? WhitelistMatchType.Exception : WhitelistMatchType.Wildcard;
            entry.Domain = domain.Substring(2);
        }
        else if (domain.StartsWith("\"") && domain.EndsWith("\""))
        {
            entry.MatchType = isException ? WhitelistMatchType.Exception : WhitelistMatchType.Substring;
            entry.Domain = domain.Trim('"');
        }
        else
        {
            entry.MatchType = isException ? WhitelistMatchType.Exception : WhitelistMatchType.Exact;
            entry.Domain = domain;
        }

        return entry;
    }
}
