using System.Windows;
using System.Windows.Controls;
using System.Windows.Media;
using frontend;

internal static partial class Program
{
    private static void TestUpdateProgress()
    {
        var progress = new UpdateProgressWindow();
        progress.Show();
        try
        {
            progress.UpdateLayout();
            CheckWindowFrame(progress);
            Check(progress.WindowStyle == WindowStyle.None && progress.Background is SolidColorBrush,
                "Update progress must use the application chrome and background");
            progress.Close();
            Check(progress.IsVisible, "An active update must not be dismissible");
            progress.Finish("Готово");
            Check(DialogChildren<TextBlock>(progress).Any(block => block.Text == "Готово"), "Update completion must remain visible");
            DialogChildren<Button>(progress).Single(button => Equals(button.Tag, "Close"))
                .RaiseEvent(new RoutedEventArgs(Button.ClickEvent));
            Check(!progress.IsVisible, "Completed updates must be dismissible through the custom title bar");
        }
        finally { progress.Dismiss(); }
    }
}
