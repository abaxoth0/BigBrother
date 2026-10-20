using frontend.Models;
using frontend.Validation;
using frontend.ViewModels;

static void Check(bool condition, string message)
{
    if (!condition) throw new Exception(message);
}

foreach (var (input, pattern, expected) in new (string, string, bool)[]
{
    ("EXAMPLE.com", "example.COM", true),
    ("www.example.com", "example.com", false),
    ("example.com", "*.example.com", true),
    ("a.b.example.com", "*.example.com", true),
    ("badexample.com", "*.example.com", false),
    ("example.com.evil.org", "*.example.com", false),
    ("mygithub.org", "\"github\"", true),
    ("example.com", "\"*.example.com\"", true),
    ("example.com", "\"\"", false),
    ("1.2.3.4", "1.2.3.4", true),
    ("1.2.3.40", "1.2.3.4", false)
}) Check(WhitelistRules.Matches(input, pattern) == expected, $"Matching {input} / {pattern}");

foreach (var invalid in new[] { "!", "!!example.com", "*. ", "*", "a.*.com", "https://example.com",
    "example.com/path", "example.com:443", "example.com.", "-bad.com", "bad-.com", "a..com",
    "999.1.2.3", "127.1", "01.2.3.4", "::1", "1.2.3.0/24", "\"\"", "\"abc", "abc\"",
    "! example.com", "foo bar", "пример.рф", new string('a', 64) + ".com", "\"" + new string('a', 254) + "\"" })
    Check(WhitelistRules.Analyze(invalid).HasErrors, $"Malformed rule accepted: {invalid}");

var valid = WhitelistRules.Analyze("# comment\r\n; comment\n\n EXAMPLE.com \r\n*.example.org\n!blocked.example.org\n\"github\"\n8.8.8.8\n!1.2.3.4");
Check(!valid.HasErrors && valid.Rules.Count == 6, "Valid syntax/comments rejected");
Check(valid.Rules[0].Line == 4, "Source line lost");
Check(valid.Issues.Any(i => !i.IsError && i.Line == 6), "Exception overlap not reported");
Check(WhitelistRules.Analyze("example.com\nEXAMPLE.COM").HasErrors, "Case-insensitive duplicate missed");
Check(WhitelistRules.Analyze("!example.com\n!EXAMPLE.COM").HasErrors, "Duplicate exceptions missed");
Check(WhitelistRules.Analyze("example.com\n!EXAMPLE.com").HasErrors, "Direct conflict missed");
Check(WhitelistRules.Analyze("!*.example.com\n*.example.com").HasErrors, "Wildcard conflict missed");
Check(!WhitelistRules.Analyze("").HasErrors, "Empty whitelist must remain valid");
foreach (var overlap in new[] { "*.example.com\n!*.sub.example.com", "\"github\"\n!\"login\"",
    "*.example.com\n!\"games\"", "example.com\n!*.example.com" })
    Check(WhitelistRules.Analyze(overlap).Issues.Any(i => !i.IsError), "Potential exception overlap missed");
Check(WhitelistRules.Analyze("*.example.com\n!*.other.org").Issues.Count == 0, "Unrelated wildcards overlap");

var rules = WhitelistRules.Analyze("*.example.com\n\"example\"\n!blocked.example.com\n8.8.8.8\n!1.2.3.4");
var blocked = WhitelistRules.Test(rules, "BLOCKED.example.com");
Check(blocked.StartsWith("Исключён") && blocked.Contains("Строка 1") && blocked.Contains("Строка 2") &&
    blocked.Contains("Строка 3"), "Exception priority or match reporting incorrect");
Check(WhitelistRules.Test(rules, "example.com").StartsWith("Разрешён"), "Wildcard base domain rejected");
Check(WhitelistRules.Test(rules, "other.org").StartsWith("Не разрешён"), "No-match result incorrect");
Check(WhitelistRules.Test(rules, "8.8.8.8").Contains("Строка 4"), "Literal IP not matched");
Check(WhitelistRules.Test(rules, "1.2.3.4").StartsWith("Исключён"), "IP exception not matched");
Check(WhitelistRules.Test(WhitelistRules.Analyze("\"8.8\""), "8.8.8.8").StartsWith("Не разрешён"),
    "Domain substring must not authorize a raw IP");
Check(WhitelistRules.Test(rules, "https://example.com").StartsWith("Введите"), "URL test accepted");
Check(WhitelistRules.Test(WhitelistRules.Analyze("!"), "example.com").StartsWith("Исправьте"), "Invalid list tested");

var vm = new WhitelistEditViewModel();
int saves = 0;
vm.Saved += (_, _) => saves++;
vm.SetCreateMode();
Check(!vm.SaveCommand.CanExecute(null), "Unnamed list can save");
vm.WhitelistName = "test";
Check(vm.SaveCommand.CanExecute(null), "Empty named list cannot save");
vm.EntriesText = "example.com\nEXAMPLE.com";
Check(!vm.SaveCommand.CanExecute(null), "Invalid list can save");
vm.SaveCommand.Execute(null);
Check(saves == 0, "Execute bypasses validation");
vm.EntriesText = "# comment\n*.example.com\n!blocked.example.com";
Check(vm.SaveCommand.CanExecute(null), "Warning blocks intentional exclusion");
Check(vm.GetEntries().Count == 2, "Comments saved as rules");
vm.EntriesText = "*.example.com";
vm.SaveCommand.Execute(null);
Check(saves == 1, "Valid save failed");
vm.SetEditMode("existing", new() { "!" });
Check(!vm.SaveCommand.CanExecute(null), "Existing invalid rules not validated");
vm.SetCreateMode();
vm.WhitelistName = "new";
Check(vm.SaveCommand.CanExecute(null) && vm.GetEntries().Count == 0, "Create reset left stale analysis");
var checker = new DomainCheckViewModel(new[]
{
    new ComposedWhitelistEntry { Value = "*.example.com", Sources = new() { "Работа", "Учёба" } },
    new ComposedWhitelistEntry { Value = "!blocked.example.com", Sources = new() { "Исключения" } },
    new ComposedWhitelistEntry { Value = "8.8.8.8", Sources = new() { "DNS" } }
});
checker.TestInput = "blocked.example.com";
Check(checker.TestResult.StartsWith("Исключён"), "Domain checker lost exception priority");
Check(checker.TestResult.Contains("*.example.com — списки: «Работа», «Учёба»") &&
    checker.TestResult.Contains("!blocked.example.com — списки: «Исключения»") &&
    !checker.TestResult.Contains("Строка"), "Matching rules lack source whitelist names");
checker.TestInput = "example.com";
Check(checker.TestResult.StartsWith("Разрешён"), "Domain checker result stale after input change");
Check(!checker.TestResult.Contains("Исключения"), "Unmatched rule source shown");
checker.TestInput = "8.8.8.8";
Check(checker.TestResult.StartsWith("Разрешён"), "Domain checker lost IPv4 support");
checker.TestInput = "https://example.com";
Check(checker.TestResult.StartsWith("Введите"), "Domain checker accepted a URL");
var emptyChecker = new DomainCheckViewModel(Array.Empty<ComposedWhitelistEntry>()) { TestInput = "example.com" };
Check(emptyChecker.TestResult.StartsWith("Не разрешён"), "Empty composition allows a domain");
var draft = new WhitelistActivationDraft();
var rows = new List<WhitelistInfo>
{
    new() { Name = "first", IsActive = true }, new() { Name = "second" }, new() { Name = "third" }
};
draft.Refresh(rows);
draft.Toggle(rows[0]);
draft.Toggle(rows[1]);
Check(draft.HasChanges && !rows[0].IsActive && rows[1].IsActive && draft.GetChanges().Count == 2,
    "Multiple toggles not staged together");
var refreshed = new List<WhitelistInfo>
{
    new() { Name = "first", IsActive = true }, new() { Name = "second" }, new() { Name = "third", IsActive = true }
};
draft.Refresh(refreshed);
Check(!refreshed[0].IsActive && refreshed[1].IsActive && refreshed[2].IsActive && draft.GetChanges().Count == 2,
    "Refresh overwrote pending edits or unrelated server updates");
draft.Cancel(refreshed);
Check(!draft.HasChanges && refreshed[0].IsActive && !refreshed[1].IsActive && refreshed[2].IsActive,
    "Cancel did not restore saved selection");
draft.Toggle(refreshed[0]);
draft.Toggle(refreshed[0]);
Check(!draft.HasChanges, "Reverting a toggle left unsaved changes");
draft.Toggle(refreshed[1]);
var submitted = draft.GetChanges();
draft.Refresh(new[] { new WhitelistInfo { Name = "first", IsActive = true },
    new WhitelistInfo { Name = "second" }, new WhitelistInfo { Name = "third", IsActive = true } });
Check(draft.HasChanges && draft.GetChanges()["second"], "Failed save refresh discarded draft");
draft.Accept(submitted);
Check(!draft.HasChanges && draft.GetActive("second"), "Successful save did not accept draft");
draft.Toggle(refreshed[1]);
draft.Rename("second", "renamed");
var renamed = new[] { new WhitelistInfo { Name = "renamed", IsActive = true } };
draft.Refresh(renamed);
Check(draft.HasChanges && !renamed[0].IsActive && draft.GetChanges().ContainsKey("renamed"), "Rename lost draft");
draft.Refresh(Array.Empty<WhitelistInfo>());
Check(!draft.HasChanges, "Deleted list retained stale draft");
var selection = new WhitelistSelection();
var selectionRows = Enumerable.Range(0, 5).Select(i => new WhitelistInfo { Name = i.ToString() }).ToList();
Check(!selection.HandleClick(selectionRows, selectionRows[1], false, false) && selectionRows.All(w => !w.IsSelected),
    "Plain row clicks should leave checkbox handling to the table");
selectionRows[1].IsSelected = true;
Check(selection.HandleClick(selectionRows, selectionRows[3], false, true) &&
    selectionRows.Select(w => w.IsSelected).SequenceEqual(new[] { false, true, true, true, false }),
    "Shift-click did not select the inclusive range");
selection.HandleClick(selectionRows, selectionRows[2], false, true);
Check(selectionRows.Select(w => w.IsSelected).SequenceEqual(new[] { false, true, true, false, false }),
    "Shift-click did not shrink the range from the original anchor");
selection.HandleClick(selectionRows, selectionRows[0], true, false);
selection.HandleClick(selectionRows, selectionRows[2], true, false);
Check(selectionRows.Select(w => w.IsSelected).SequenceEqual(new[] { true, true, false, false, false }),
    "Ctrl-click did not toggle individual checkboxes while retaining the others");
selection.HandleClick(selectionRows, selectionRows[4], true, true);
Check(selectionRows.All(w => w.IsSelected), "Ctrl+Shift-click did not preserve selection outside the range");
var sortedRows = selectionRows.AsEnumerable().Reverse().ToList();
selection.HandleClick(sortedRows, selectionRows[4], false, false);
selection.HandleClick(sortedRows, selectionRows[2], false, true);
Check(selectionRows.Select(w => w.IsSelected).SequenceEqual(new[] { false, false, true, true, true }),
    "Shift-click did not follow the displayed sort order");
selection.HandleClick(selectionRows, selectionRows[0], false, false);
selectionRows.RemoveAt(0);
selection.HandleClick(selectionRows, selectionRows[3], false, true);
Check(selectionRows.Count(w => w.IsSelected) == 1 && selectionRows[3].IsSelected,
    "Deleting the selection anchor broke subsequent Shift-clicks");
Console.WriteLine("Whitelist validation, matching, editor, domain checker, activation draft and selection checks passed.");
