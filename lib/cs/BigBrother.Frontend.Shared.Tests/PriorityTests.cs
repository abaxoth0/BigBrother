using frontend.Models;
using frontend.Validation;

static class PriorityTests
{
    static void Check(bool condition, string message)
    {
        if (!condition) throw new Exception(message);
    }

    public static void Run()
    {
        var analysis = WhitelistRules.Analyze("# default\n\n[10]\ngithub.com\n!gist.github.com\n[20]other.org:443\n[0]base.org");
        Check(!analysis.HasErrors && analysis.DefaultPriority == 10 && analysis.Rules.Count == 4,
            "Leading declaration must set the default without counting as a rule");
        Check(analysis.Rules.Select(r => r.Priority).SequenceEqual(new[] { 10, 10, 20, 0 }) && analysis.Rules[0].Line == 4,
            "Default inheritance, explicit overrides or source lines were lost");
        Check(WhitelistRules.Analyze("[10]github.com\nother.org").Rules.Select(r => r.Priority).SequenceEqual(new[] { 10, 0 }),
            "A prefix on the first rule must not set the default");
        Check(WhitelistRules.Analyze("[10]\n# no rules").Rules.Count == 0, "Declaration-only list must remain empty");
        foreach (var invalid in new[] {
            "[]example.com", "[-1]example.com", "[+1]example.com", "[2147483648]example.com",
            "[99999999999999999999999]example.com", "[default 10]", "[10example.com", "[1][2]example.com", "![10]example.com",
            "example.com\n[10]", "[10]\n[20]" })
            Check(WhitelistRules.Analyze(invalid).HasErrors, $"Malformed priority accepted: {invalid}");
        Check(WhitelistRules.Analyze("[2147483647]example.com").Rules.Single().Priority == int.MaxValue,
            "Maximum supported priority rejected");
        Check(WhitelistRules.Analyze("[10]\nexample.com\n[10]EXAMPLE.COM").HasErrors,
            "Equivalent inherited and explicit rules must be detected as duplicates");
        var policy = WhitelistRules.Analyze("!\"github\"\n[10]*.github.com:443\n[20]!gist.github.com");
        Check(!policy.HasErrors, "Priority override rejected by validation");
        foreach (var domain in new[] { "github.com", "api.github.com" })
        {
            Check(WhitelistRules.Test(policy, domain + ":443").StartsWith("Разрешён"), "Higher-priority allow did not override exclusion");
            Check(WhitelistRules.Test(policy, domain + ":80").StartsWith("Исключён"), "Port-limited override allowed other ports");
            Check(WhitelistRules.Test(policy, domain).StartsWith("Доступ зависит"), "Checker hid a priority-dependent port decision");
        }
        Check(WhitelistRules.Test(policy, "gist.github.com:443").StartsWith("Исключён"), "Higher-priority denial lost");
        Check(WhitelistRules.Test(policy, "other.org:443").StartsWith("Не разрешён"), "Priority allowed an unrelated host");
        var exact = WhitelistRules.Analyze("!github.com\n[10]github.com");
        Check(!exact.HasErrors && WhitelistRules.Test(exact, "github.com:443").StartsWith("Разрешён"), "Exact override rejected");
        var ties = WhitelistRules.Analyze("[10]github.com\n[10]!github.com", allowConflicts: true);
        Check(!ties.HasErrors && WhitelistRules.Test(ties, "github.com:443").StartsWith("Исключён"), "Equal-priority exclusion must win");
        var portIndependent = WhitelistRules.Analyze("[10]github.com\n!github.com:443");
        Check(WhitelistRules.Test(portIndependent, "github.com").StartsWith("Разрешён"), "Lower-priority port exclusion caused false ambiguity");
        var ip = WhitelistRules.Analyze("!1.1.1.1\n[10]1.1.1.1:{80|443}");
        Check(WhitelistRules.Test(ip, "1.1.1.1:443").StartsWith("Разрешён") &&
            WhitelistRules.Test(ip, "1.1.1.1:22").StartsWith("Исключён"), "IP priority and port comparison differ from domains");
        var pretty = WhitelistEntry.Parse("[20]!\"github\":443");
        Check(pretty.Priority == 20 && pretty.Domain == "github" && pretty.Ports == "443" && pretty.MatchType == WhitelistMatchType.Exception &&
            pretty.GetPrettyDescription().Contains("приоритет: 20"), "Client display lost priority, exception or port information");
        var conflict = new WhitelistConflict { Pattern = "github.com", AllowedPriority = 10, ExcludedPriority = 20 };
        Check(conflict.AllowedRule == "[10]github.com" && conflict.ExcludedRule == "[20]!github.com" && conflict.Resolution == "Исключено",
            "Conflict view lost priority or constructed invalid rule syntax");
        conflict.AllowedPriority = 30;
        Check(conflict.Resolution == "Разрешено", "Conflict resolution does not reflect the winning allow priority");
        Console.WriteLine("PASS: default and per-rule priorities, validation, ports, checker and client display");
    }
}
