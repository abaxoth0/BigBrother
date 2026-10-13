using System.Windows;
using System.Windows.Controls;
using System.Windows.Threading;
using frontend;
using frontend.ViewModels;

namespace frontend.Views;

public partial class WhitelistEditWindow : ChromeWindow
{
    private readonly WhitelistEditViewModel _viewModel;
    private bool _lineNumberUpdatePending;

    public WhitelistEditWindow()
    {
        InitializeComponent();
        _viewModel = new WhitelistEditViewModel();
        DataContext = _viewModel;
    }

    public WhitelistEditViewModel ViewModel => _viewModel;

    private void EntriesEditor_LayoutChanged(object sender, RoutedEventArgs e) => QueueLineNumberUpdate();
    private void EntriesEditor_TextChanged(object sender, TextChangedEventArgs e) => QueueLineNumberUpdate();
    private void EntriesEditor_ScrollChanged(object sender, ScrollChangedEventArgs e) => QueueLineNumberUpdate();

    private void QueueLineNumberUpdate()
    {
        if (_lineNumberUpdatePending) return;
        _lineNumberUpdatePending = true;
        Dispatcher.BeginInvoke(DispatcherPriority.Loaded, new Action(() =>
        {
            _lineNumberUpdatePending = false;
            UpdateLineNumbers();
        }));
    }

    private void UpdateLineNumbers()
    {
        if (!EntriesEditor.IsLoaded) return;
        LineNumberGutter.Children.Clear();
        int first = EntriesEditor.GetFirstVisibleLineIndex();
        int last = EntriesEditor.GetLastVisibleLineIndex();
        int digits = Math.Max(1, EntriesEditor.LineCount).ToString().Length;
        LineNumberGutter.Width = Math.Max(32, digits * EntriesEditor.FontSize * 0.65 + 12);

        // Use the editor's actual line positions, including partially scrolled
        // lines. Only visible numbers are drawn, even for large whitelists.
        for (int line = first; line >= 0 && line <= last; line++)
        {
            int index = EntriesEditor.GetCharacterIndexFromLineIndex(line);
            var rect = EntriesEditor.GetRectFromCharacterIndex(index);
            if (rect.IsEmpty) continue;
            var number = new TextBlock
            {
                Text = (line + 1).ToString(),
                FontFamily = EntriesEditor.FontFamily,
                FontSize = EntriesEditor.FontSize,
                Foreground = EntriesEditor.Foreground,
                Opacity = 0.6,
                Width = LineNumberGutter.Width - 6,
                TextAlignment = TextAlignment.Right,
                LineHeight = rect.Height,
                LineStackingStrategy = LineStackingStrategy.BlockLineHeight
            };
            Canvas.SetTop(number, rect.Top);
            LineNumberGutter.Children.Add(number);
        }
    }
}
