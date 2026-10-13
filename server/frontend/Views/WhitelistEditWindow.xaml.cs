using System.Windows;
using System.Windows.Controls;
using System.Windows.Media;
using System.Windows.Threading;
using frontend;
using frontend.ViewModels;

namespace frontend.Views;

public partial class WhitelistEditWindow : ChromeWindow
{
    private readonly WhitelistEditViewModel _viewModel;
    private bool _lineAdornmentUpdatePending;

    public WhitelistEditWindow()
    {
        InitializeComponent();
        _viewModel = new WhitelistEditViewModel();
        _viewModel.PropertyChanged += (_, e) =>
        {
            if (e.PropertyName == nameof(WhitelistEditViewModel.ValidationIssues)) QueueLineAdornmentUpdate();
        };
        DataContext = _viewModel;
    }

    public WhitelistEditViewModel ViewModel => _viewModel;

    private void EntriesEditor_LayoutChanged(object sender, RoutedEventArgs e) => QueueLineAdornmentUpdate();
    private void EntriesEditor_TextChanged(object sender, TextChangedEventArgs e) => QueueLineAdornmentUpdate();
    private void EntriesEditor_ScrollChanged(object sender, ScrollChangedEventArgs e) => QueueLineAdornmentUpdate();

    private void QueueLineAdornmentUpdate()
    {
        if (_lineAdornmentUpdatePending) return;
        _lineAdornmentUpdatePending = true;
        Dispatcher.BeginInvoke(DispatcherPriority.Loaded, new Action(() =>
        {
            _lineAdornmentUpdatePending = false;
            UpdateLineAdornments();
        }));
    }

    private void UpdateLineAdornments()
    {
        if (!EntriesEditor.IsLoaded) return;
        LineNumberGutter.Children.Clear();
        LineHighlightCanvas.Children.Clear();
        var issuesByLine = _viewModel.ValidationIssues.GroupBy(issue => issue.Line)
            .ToDictionary(group => group.Key, group => group.Any(issue => issue.IsError));
        int first = EntriesEditor.GetFirstVisibleLineIndex();
        int last = EntriesEditor.GetLastVisibleLineIndex();
        int digits = Math.Max(1, EntriesEditor.LineCount).ToString().Length;
        LineNumberGutter.Width = Math.Max(32, digits * EntriesEditor.FontSize * 0.65 + 12);

        // Use the editor's actual line positions, including partially scrolled
        // lines. Only visible numbers and backgrounds are drawn.
        for (int line = first; line >= 0 && line <= last; line++)
        {
            int index = EntriesEditor.GetCharacterIndexFromLineIndex(line);
            var rect = EntriesEditor.GetRectFromCharacterIndex(index);
            if (rect.IsEmpty) continue;
            if (issuesByLine.TryGetValue(line + 1, out bool isError))
            {
                var highlight = new Border
                {
                    Width = LineHighlightCanvas.ActualWidth,
                    Height = rect.Height,
                    Background = (Brush)FindResource(isError ? "LogLevelErrorBg" : "LogLevelWarningBg"),
                    Opacity = 0.4
                };
                Canvas.SetTop(highlight, rect.Top);
                LineHighlightCanvas.Children.Add(highlight);
            }
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
