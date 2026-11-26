using System.IO;
using System.Windows;
using System.Windows.Controls;
using frontend.ViewModels;
using frontend.Views;

internal static partial class Program
{
    private static void TestProcessBlacklistEditor()
    {
        string[] serverRules = ["game.exe"];
        bool loadFailed = true;
        var model = new ProcessBlacklistViewModel(
            () => loadFailed ? Task.FromException<List<string>>(new IOException("offline"))
                             : Task.FromResult(serverRules.ToList()),
            lines =>
            {
                if (lines.Contains("invalid")) throw new IOException("invalid rule");
                serverRules = lines;
                return Task.CompletedTask;
            });

        var view = new ProcessBlacklistView { DataContext = model };
        var window = new Window { Content = view, Width = 720, Height = 480 };
        try
        {
            window.Show();
            FlushLayout(window);
            var editor = VisualChildren<TextBox>(view).Single(box => box.AcceptsReturn);
            var buttons = VisualChildren<Button>(view).ToList();
            var save = buttons.Single(button => button.Command == model.SaveCommand);
            var cancel = buttons.Single(button => button.Command == model.CancelCommand);
            var retry = buttons.Single(button => button.Command == model.RetryCommand);

            // The test transport completes synchronously on the UI thread.
            model.LoadAsync().GetAwaiter().GetResult();
            FlushLayout(window);
            Check(!editor.IsEnabled && !save.IsEnabled && retry.IsVisible && model.Status == "offline",
                "A failed load must offer a retry before the rules can be edited");

            loadFailed = false;
            retry.Command.Execute(null);
            FlushLayout(window);
            Check(editor.IsEnabled && editor.Text == "game.exe" && !retry.IsVisible,
                "Retry must load the saved rules into the tab");
            var firstLine = editor.GetRectFromCharacterIndex(0);
            Check(!firstLine.IsEmpty && firstLine.Top <= editor.Padding.Top + editor.FontSize,
                "The first rule must appear at the top of the blacklist editor");

            editor.SetCurrentValue(TextBox.TextProperty, "invalid");
            save.Command.Execute(null);
            model.LoadAsync().GetAwaiter().GetResult();
            FlushLayout(window);
            Check(editor.Text == "invalid" && model.Status == "invalid rule" && cancel.IsEnabled,
                "A rejected save and tab reentry must preserve the draft");
            cancel.Command.Execute(null);
            Check(editor.Text == "game.exe", "Cancel must restore the saved rules inline");

            string rules = "game*.exe" + Environment.NewLine + "!game-editor.exe";
            editor.SetCurrentValue(TextBox.TextProperty, rules);
            save.Command.Execute(null);
            FlushLayout(window);
            Check(serverRules.SequenceEqual(new[] { "game*.exe", "!game-editor.exe" }) && !save.IsEnabled,
                "Save must submit one rule per line and mark the draft as saved");
            editor.SetCurrentValue(TextBox.TextProperty, "");
            cancel.Command.Execute(null);
            Check(editor.Text == rules, "Cancel must use the latest successful save");
            editor.SetCurrentValue(TextBox.TextProperty, "");
            save.Command.Execute(null);
            Check(string.Join("", serverRules) == "", "An empty blacklist must be saved");
            Console.WriteLine("PASS: inline blacklist loading, retry, top alignment, save, validation and cancel");
        }
        finally
        {
            window.Close();
        }
    }
}
