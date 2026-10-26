using System.Net;
using System.Net.Sockets;

namespace frontend.Validation;

public record PortRange(int First, int Last);
public record WhitelistRule(int Line, string Text, string Pattern, bool IsException, bool IsIp)
{
    public IReadOnlyList<PortRange> Ports { get; init; } = [];
    public bool MatchesPort(int port) => Ports.Count == 0 || Ports.Any(p => port >= p.First && port <= p.Last);
}
public record WhitelistIssue(int Line, string Message, bool IsError = true)
{
    public override string ToString() => $"{(IsError ? "Ошибка" : "Предупреждение")}, строка {Line}: {Message}";
}

public class WhitelistAnalysis
{
    public List<WhitelistRule> Rules { get; } = new();
    public List<WhitelistIssue> Issues { get; } = new();
    public bool HasErrors => Issues.Any(i => i.IsError);
}

public static class WhitelistRules
{
    public static WhitelistAnalysis Analyze(string text)
    {
        var result = new WhitelistAnalysis();
        var seen = new Dictionary<string, WhitelistRule>(StringComparer.OrdinalIgnoreCase);
        var lines = text.Split('\n');
        for (int i = 0; i < lines.Length; i++)
        {
            var entry = lines[i].Trim();
            if (entry.Length == 0 || entry.StartsWith('#') || entry.StartsWith(';')) continue;
            bool exception = entry.StartsWith('!');
            var pattern = exception ? entry[1..] : entry;
            if (!TrySplitPorts(pattern, out pattern, out var ports) || !ValidPattern(pattern, out bool isIp))
            {
                result.Issues.Add(new(i + 1, "ожидается домен, IPv4, *.домен или непустая подстрока в кавычках; порты: :443, :443|80 или :{0-1023|5432|2000-3000} (0–65535); IPv6, URL и CIDR не поддерживаются."));
                continue;
            }
            var rule = new WhitelistRule(i + 1, entry, pattern.ToLowerInvariant(), exception, isIp) { Ports = ports };
            if (rule.IsException && rule.Ports.Count > 0 && rule.MatchesPort(53))
                result.Issues.Add(new(i + 1, "нельзя заблокировать порт 53: DNS-трафик TCP/UDP всегда разрешён. Исключите порт 53 из правила."));
            if (seen.TryGetValue(entry, out var duplicate))
                result.Issues.Add(new(i + 1, $"повтор правила из строки {duplicate.Line}."));
            else
                seen.Add(entry, rule);
            result.Rules.Add(rule);
        }

        foreach (var exception in result.Rules.Where(r => r.IsException))
        foreach (var allow in result.Rules.Where(r => !r.IsException))
        {
            if (!PortsOverlap(exception, allow)) continue;
            if (exception.Pattern == allow.Pattern && exception.Ports.SequenceEqual(allow.Ports))
                result.Issues.Add(new(exception.Line, $"конфликт с разрешением в строке {allow.Line}: исключение имеет приоритет."));
            else if (Overlaps(exception, allow))
                result.Issues.Add(new(exception.Line, $"возможное пересечение с разрешением в строке {allow.Line}: совпавшие адреса будут исключены.", false));
        }
        return result;
    }

    private static bool TrySplitPorts(string value, out string host, out IReadOnlyList<PortRange> ports)
    {
        int colon = value.IndexOf(':');
        host = colon < 0 ? value : value[..colon];
        ports = [];
        if (colon < 0) return true;
        var selector = value[(colon + 1)..];
        if (selector.StartsWith('{'))
        {
            if (!selector.EndsWith('}')) return false;
            selector = selector[1..^1];
        }
        var ranges = new List<PortRange>();
        foreach (var item in selector.Split('|'))
        {
            var ends = item.Split('-');
            if (ends.Length is < 1 or > 2 || !TryPort(ends[0], out int first)) return false;
            int last = first;
            if (ends.Length == 2 && !TryPort(ends[1], out last)) return false;
            if (first > last) return false;
            ranges.Add(new(first, last));
        }
        var merged = new List<PortRange>();
        foreach (var range in ranges.OrderBy(p => p.First).ThenBy(p => p.Last))
        {
            if (merged.Count > 0 && range.First <= merged[^1].Last + 1)
                merged[^1] = new(merged[^1].First, Math.Max(merged[^1].Last, range.Last));
            else merged.Add(range);
        }
        ports = merged;
        return true;
    }

    private static bool TryPort(string value, out int port)
    {
        port = 0;
        return value.Length > 0 && value.All(char.IsAsciiDigit) && int.TryParse(value, out port) && port <= 65535;
    }

    private static bool PortsOverlap(WhitelistRule a, WhitelistRule b) =>
        a.Ports.Count == 0 || b.Ports.Count == 0 ||
        a.Ports.Any(x => b.Ports.Any(y => x.First <= y.Last && y.First <= x.Last));

    private static bool ValidPattern(string pattern, out bool isIp)
    {
        isIp = IsIpv4(pattern);
        if (pattern.Length == 0 || pattern.Length > 255) return false;
        if (isIp) return true;
        if (pattern.StartsWith('"') && pattern.EndsWith('"') && pattern.Length > 2)
            return pattern[1..^1].All(c => char.IsAsciiLetterOrDigit(c) || c is '.' or '-' or '_' or '*');
        if (pattern.StartsWith("*.")) pattern = pattern[2..];
        return IsDomain(pattern);
    }

    private static bool IsIpv4(string value) => value.Split('.').Length == 4 &&
        value.Split('.').All(part => part.Length is >= 1 and <= 3 &&
            part.All(char.IsAsciiDigit) && (part.Length == 1 || part[0] != '0') && byte.TryParse(part, out _)) &&
        IPAddress.TryParse(value, out var ip) && ip.AddressFamily == AddressFamily.InterNetwork;

    private static bool IsDomain(string value) => value.Length is > 0 and <= 253 &&
        !value.All(c => char.IsAsciiDigit(c) || c == '.') &&
        value.Split('.').All(label => label.Length is > 0 and <= 63 &&
            char.IsAsciiLetterOrDigit(label[0]) && char.IsAsciiLetterOrDigit(label[^1]) &&
            label.All(c => char.IsAsciiLetterOrDigit(c) || c == '-'));

    // Mirrors DnsCheckDomainLower, including the wildcard fallback for quoted patterns.
    public static bool Matches(string value, string pattern)
    {
        value = value.ToLowerInvariant();
        pattern = pattern.ToLowerInvariant();
        if (pattern.StartsWith('"') && pattern.EndsWith('"'))
        {
            pattern = pattern[1..^1];
            if (pattern.Length == 0) return false;
            if (value.Contains(pattern, StringComparison.Ordinal)) return true;
        }
        if (pattern.StartsWith("*."))
            return value == pattern[2..] || value.EndsWith("." + pattern[2..], StringComparison.Ordinal);
        return value == pattern;
    }

    private static bool Overlaps(WhitelistRule a, WhitelistRule b)
    {
        if (a.Pattern == b.Pattern) return true;
        if (a.IsIp || b.IsIp) return false;
        bool aSubstring = a.Pattern.StartsWith('"');
        bool bSubstring = b.Pattern.StartsWith('"');
        // Two substrings can coexist in one domain; a substring can also occur
        // in an arbitrary subdomain of a wildcard, even if its suffix differs.
        if ((aSubstring && (bSubstring || b.Pattern.StartsWith("*."))) ||
            (bSubstring && a.Pattern.StartsWith("*."))) return true;
        return Matches(a.Pattern.Trim('"').Replace("*.", ""), b.Pattern) ||
            Matches(b.Pattern.Trim('"').Replace("*.", ""), a.Pattern);
    }

    public static string Test(WhitelistAnalysis analysis, string input, Func<WhitelistRule, string>? describeRule = null)
    {
        input = input.Trim();
        int? port = null;
        int colon = input.IndexOf(':');
        if (colon >= 0)
        {
            if (!TryPort(input[(colon + 1)..], out int number)) return "Введите домен или IPv4 с необязательным портом 0–65535.";
            port = number;
            input = input[..colon];
        }
        bool isIp = IsIpv4(input);
        if (!isIp && !IsDomain(input)) return "Введите домен или IPv4 без URL и маски, с необязательным :портом.";
        if (analysis.HasErrors) return "Исправьте ошибки списка перед проверкой правил.";
        var matches = analysis.Rules.Where(r => r.IsIp == isIp && Matches(input, r.Pattern) && (!port.HasValue || r.MatchesPort(port.Value))).ToList();
        var exceptions = matches.Where(r => r.IsException).ToList();
        bool dependsOnPort = !port.HasValue && matches.Any(r => r.Ports.Count > 0) &&
            !exceptions.Any(r => r.Ports.Count == 0);
        string outcome = dependsOnPort ? "Доступ зависит от порта: укажите :порт для точной проверки" : exceptions.Count > 0 ? "Исключён (исключения имеют приоритет)" :
            matches.Count > 0 ? "Разрешён правилами" : "Не разрешён: совпадений нет";
        return outcome + (matches.Count == 0 ? "." : ":\n" +
            string.Join("\n", matches.Select(r => describeRule?.Invoke(r) ?? $"Строка {r.Line}: {r.Text}"))) +
            "\nПроверяется только список. DNS-кэш и общие IP доменов могут влиять на доступ клиента.";
    }
}
