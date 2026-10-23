using frontend.Models;
using frontend.Validation;

static class WhitelistTests
{
    static void Check(bool condition, string message)
    {
        if (!condition) throw new Exception(message);
    }

    public static void Run()
    {
        string[] selectors = ["", ":443", ":{0-1023}", ":443|80|53|20|22", ":{0-1023|5432|2000-3000}"];
        foreach (string host in new[] { "1.1.1.1", "example.com", "*.example.com", "\"example\"" })
        foreach (string selector in selectors)
        foreach (string prefix in new[] { "", "!" })
        {
            var analysis = WhitelistRules.Analyze(prefix + host + selector);
            Check(!analysis.HasErrors && analysis.Rules.Single().Pattern == host, "Valid host/port combinations");
        }
        for (int i = 0; i < selectors.Length; i++)
        {
            var rule = WhitelistRules.Analyze("1.1.1.1" + selectors[i]).Rules.Single();
            for (int port = 0; port <= 65535; port++)
            {
                bool expected = i == 0 || (i == 1 && port == 443) || (i == 2 && port <= 1023) ||
                    (i == 3 && port is 443 or 80 or 53 or 20 or 22) ||
                    (i == 4 && (port <= 1023 || port == 5432 || port is >= 2000 and <= 3000));
                Check(rule.MatchesPort(port) == expected, "Port sets and inclusive boundaries");
            }
        }
        foreach (string selector in new[] { "", "{}", "{1", "1}", "{1}}", "{1|}", "|1", "1||2", "1|", "-1", "1-", "2-1", "65536", "1-65536", "9999999999999", "1:2", "+1", "1 2", "{1-2-3}" })
            Check(WhitelistRules.Analyze("1.1.1.1:" + selector).HasErrors, "Reject malformed ports: " + selector);
        foreach (string rule in new[] { "https://example.com:443", "[::1]:443", "1.1.1.1/24:443", "*.1.1.1.1:443", "\"\":443" })
            Check(WhitelistRules.Analyze(rule).HasErrors, "Host validation retained");

        var partial = WhitelistRules.Analyze("1.1.1.1\n!1.1.1.1:443");
        Check(!partial.HasErrors && partial.Issues.Any(i => !i.IsError), "Partial exception is a warning");
        Check(WhitelistRules.Test(partial, "1.1.1.1:80").StartsWith("Разрешён"), "Allow other ports");
        Check(WhitelistRules.Test(partial, "1.1.1.1:443").StartsWith("Исключён"), "Exception port wins");
        Check(WhitelistRules.Test(partial, "1.1.1.1").StartsWith("Доступ зависит"), "Do not claim all ports allowed or blocked");
        Check(WhitelistRules.Analyze("1.1.1.1:80\n!1.1.1.1:443").Issues.Count == 0, "Disjoint ports do not conflict");
        Check(WhitelistRules.Analyze("1.1.1.1:80\n!1.1.1.1:80").HasErrors, "Exact conflicts remain errors");
        Check(WhitelistRules.Analyze("example.com:443\nEXAMPLE.COM:443").HasErrors, "Case-insensitive duplicates remain errors");
        var ranges = WhitelistRules.Analyze("*.example.com:{0-1023|5432|2000-3000}");
        Check(WhitelistRules.Test(ranges, "www.example.com:5432").StartsWith("Разрешён"), "Check domain with selected port");
        Check(WhitelistRules.Test(ranges, "www.example.com:3001").StartsWith("Не разрешён"), "Reject unselected port");
        Check(WhitelistRules.Test(ranges, "www.example.com:65536").StartsWith("Введите"), "Validate checker port");
        Check(WhitelistRules.Test(ranges, "www.example.com:80|443").StartsWith("Введите"), "Checker accepts a single port");
        Check(WhitelistRules.Analyze("# comment\n; comment\n").Rules.Count == 0, "Comments remain ignored");
        var pretty = WhitelistEntry.Parse("\"example\":{443|80}");
        Check(pretty.Domain == "example" && pretty.MatchType == WhitelistMatchType.Substring && pretty.Ports == "443|80", "Pretty view separates ports from host");
        Check(pretty.GetPrettyDescription().Contains("порты: 443|80"), "Pretty view shows ports");
        Console.WriteLine("PASS: whitelist ports, ranges, unions, validation, conflicts, checker and display");
    }
}
