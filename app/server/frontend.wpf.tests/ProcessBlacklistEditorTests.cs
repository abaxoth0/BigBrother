using System.Windows.Controls;
using frontend.Services;
using frontend.Views;

internal static partial class Program
{
    private static void TestProcessBlacklistEditor()
    {
        using var ipc = new IpcService();
        var dialog = new ProcessBlacklistWindow(ipc, ["game.exe"]);
        try
        {
            dialog.Show();
            FlushLayout(dialog);

            var editor = VisualChildren<TextBox>(dialog).Single(box => box.AcceptsReturn);
            var firstLine = editor.GetRectFromCharacterIndex(0);
            Check(!firstLine.IsEmpty && firstLine.Top <= editor.Padding.Top + editor.FontSize,
                "The first rule must appear at the top of the blacklist editor");
        }
        finally
        {
            dialog.Close();
        }
    }
}
