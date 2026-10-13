using System.Net;
using System.Net.Sockets;

namespace frontend.Validation;

public record WhitelistRule(int Line, string Text, string Pattern, bool IsException, bool IsIp);
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
            if (!ValidPattern(pattern, out bool isIp))
            {
                result.Issues.Add(new(i + 1, "ожидается домен, IPv4, *.домен или непустая подстрока в кавычках; IPv6, URL и CIDR не поддерживаются."));
                continue;
            }
            var rule = new WhitelistRule(i + 1, entry, pattern.ToLowerInvariant(), exception, isIp);
            if (seen.TryGetValue(entry, out var duplicate))
                result.Issues.Add(new(i + 1, $"повтор правила из строки {duplicate.Line}."));
            else
                seen.Add(entry, rule);
            result.Rules.Add(rule);
        }

        foreach (var exception in result.Rules.Where(r => r.IsException))
        foreach (var allow in result.Rules.Where(r => !r.IsException))
        {
            if (exception.Pattern == allow.Pattern)
                result.Issues.Add(new(exception.Line, $"конфликт с разрешением в строке {allow.Line}: исключение имеет приоритет."));
            else if (Overlaps(exception, allow))
                result.Issues.Add(new(exception.Line, $"возможное пересечение с разрешением в строке {allow.Line}: совпавшие адреса будут исключены.", false));
        }
        return result;
    }

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

    public static string Test(WhitelistAnalysis analysis, string input)
    {
        input = input.Trim();
        bool isIp = IsIpv4(input);
        if (!isIp && !IsDomain(input)) return "Введите домен или IPv4 без URL, порта и маски.";
        if (analysis.HasErrors) return "Исправьте ошибки списка перед проверкой правил.";
        var matches = analysis.Rules.Where(r => r.IsIp == isIp && Matches(input, r.Pattern)).ToList();
        var exceptions = matches.Where(r => r.IsException).ToList();
        string outcome = exceptions.Count > 0 ? "Исключён (исключения имеют приоритет)" :
            matches.Count > 0 ? "Разрешён правилами" : "Не разрешён: совпадений нет";
        return outcome + (matches.Count == 0 ? "." : ":\n" +
            string.Join("\n", matches.Select(r => $"Строка {r.Line}: {r.Text}"))) +
            "\nПроверяется только список. DNS-кэш и общие IP доменов могут влиять на доступ клиента.";
    }
}
