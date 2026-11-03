using System.Windows;
using System.Windows.Controls;
using System.Windows.Data;
using System.Windows.Shell;

namespace frontend;

public class DialogWindow : ChromeWindow
{
    private readonly ContentPresenter _body = new();
    protected Button CloseButton { get; } = new() { Content = "×", Tag = "Close", ToolTip = "Закрыть" };

    protected UIElement DialogContent
    {
        get => (UIElement)_body.Content;
        set => _body.Content = value;
    }

    public DialogWindow()
    {
        Width = 560;
        MaxWidth = Math.Max(320, SystemParameters.WorkArea.Width - 32);
        MaxHeight = Math.Max(240, SystemParameters.WorkArea.Height - 32);
        SizeToContent = SizeToContent.Height;
        ResizeMode = ResizeMode.NoResize;
        WindowStyle = WindowStyle.None;
        WindowStartupLocation = WindowStartupLocation.CenterOwner;
        ShowInTaskbar = false;
        SetResourceReference(BackgroundProperty, "WindowBackground");
        SetResourceReference(ForegroundProperty, "TitleBarText");
        var chrome = new WindowChrome
        {
            ResizeBorderThickness = new Thickness(0), GlassFrameThickness = new Thickness(0),
            UseAeroCaptionButtons = false
        };
        BindingOperations.SetBinding(chrome, WindowChrome.CaptionHeightProperty,
            new Binding("ActualHeight") { Source = CloseButton });
        WindowChrome.SetWindowChrome(this, chrome);
        var title = new TextBlock
        {
            Margin = new Thickness(14, 0, 14, 0), VerticalAlignment = VerticalAlignment.Center,
            FontWeight = FontWeights.SemiBold, TextTrimming = TextTrimming.CharacterEllipsis
        };
        title.SetBinding(TextBlock.TextProperty, new Binding(nameof(Title)) { Source = this });
        CloseButton.SetResourceReference(StyleProperty, "TitleBarButton");
        WindowChrome.SetIsHitTestVisibleInChrome(CloseButton, true);
        CloseButton.Click += CloseButton_Click;
        var header = new DockPanel();
        DockPanel.SetDock(CloseButton, Dock.Right);
        header.Children.Add(CloseButton);
        header.Children.Add(title);
        var border = new Border { BorderThickness = new Thickness(0, 0, 0, 1), Child = header };
        border.SetResourceReference(Border.BackgroundProperty, "TitleBarBackground");
        border.SetResourceReference(Border.BorderBrushProperty, "BorderBrush");
        var grid = new Grid();
        grid.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });
        grid.RowDefinitions.Add(new RowDefinition { Height = new GridLength(1, GridUnitType.Star) });
        grid.Children.Add(border);
        Grid.SetRow(_body, 1);
        grid.Children.Add(_body);
        Content = grid;
    }

    public static Window? FindOwner() => Application.Current?.Windows.OfType<Window>()
        .FirstOrDefault(window => window.IsActive && window.IsVisible) ??
        (Application.Current?.MainWindow is { IsVisible: true } main ? main : null);
}
