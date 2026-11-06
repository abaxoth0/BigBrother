using System.ComponentModel;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
using frontend.ViewModels;

namespace frontend.Views;

public partial class NetworkSchemeView : UserControl
{
    private Border? _dragged;
    private Point _offset;
    private Point _start;
    private Point? _panStart;
    private Point _panOffset;
    private Point? _zoomAnchor;
    private int _zIndex;
    private NetworkSchemeViewModel? Model => DataContext as NetworkSchemeViewModel;

    public static readonly DependencyProperty ZoomProperty = DependencyProperty.Register(nameof(Zoom), typeof(double),
        typeof(NetworkSchemeView), new PropertyMetadata(1d, (sender, _) => ((NetworkSchemeView)sender).ApplyZoom(),
            (_, value) => double.IsFinite((double)value) ? Math.Clamp((double)value, 0.1, 2) : 1d));

    public double Zoom { get => (double)GetValue(ZoomProperty); set => SetValue(ZoomProperty, value); }

    public NetworkSchemeView()
    {
        InitializeComponent();
        Loaded += (_, _) =>
        {
            AppearanceSettings.Current.PropertyChanged += AppearanceChanged;
            ApplyNodeSize();
        };
        Unloaded += (_, _) =>
        {
            EndDrag();
            EndPan();
            AppearanceSettings.Current.PropertyChanged -= AppearanceChanged;
        };
        DataContextChanged += (_, _) => ApplyNodeSize();
    }

    private void AppearanceChanged(object? sender, PropertyChangedEventArgs e)
    {
        if (e.PropertyName is nameof(AppearanceSettings.TextFontSize) or nameof(AppearanceSettings.InterfaceFontSize)) ApplyNodeSize();
    }

    private void ApplyNodeSize() => Model?.SetNodeScale(Math.Max(AppearanceSettings.Current.TextFontSize,
        AppearanceSettings.Current.InterfaceFontSize) / 14.0);

    private void MapScroll_SizeChanged(object sender, SizeChangedEventArgs e)
    {
        var previous = MapFrame.Padding;
        MapFrame.Padding = new Thickness(e.NewSize.Width / 2, e.NewSize.Height / 2,
            e.NewSize.Width / 2, e.NewSize.Height / 2);
        MapScroll.ScrollToHorizontalOffset(MapScroll.HorizontalOffset + MapFrame.Padding.Left - previous.Left);
        MapScroll.ScrollToVerticalOffset(MapScroll.VerticalOffset + MapFrame.Padding.Top - previous.Top);
    }

    private void ApplyZoom()
    {
        var anchor = _zoomAnchor ?? new Point(MapScroll.ViewportWidth / 2, MapScroll.ViewportHeight / 2);
        var world = MapScroll.TranslatePoint(anchor, MapCanvas);
        MapZoom.ScaleX = MapZoom.ScaleY = Zoom;
        MapScroll.UpdateLayout();
        MapScroll.ScrollToHorizontalOffset(MapFrame.Padding.Left + world.X * Zoom - anchor.X);
        MapScroll.ScrollToVerticalOffset(MapFrame.Padding.Top + world.Y * Zoom - anchor.Y);
    }

    private void MapScroll_MouseWheel(object sender, MouseWheelEventArgs e)
    {
        if (!Keyboard.Modifiers.HasFlag(ModifierKeys.Shift)) return;
        _zoomAnchor = e.GetPosition(MapScroll);
        try { Zoom = Math.Round(Zoom + e.Delta / 120d * 0.1, 3); }
        finally { _zoomAnchor = null; }
        e.Handled = true;
    }

    private void Map_MouseDown(object sender, MouseButtonEventArgs e)
    {
        if (_dragged != null) return;
        MapFrame.Focus();
        _panStart = e.GetPosition(MapScroll);
        _panOffset = new Point(MapScroll.HorizontalOffset, MapScroll.VerticalOffset);
        MapFrame.CaptureMouse();
        MapFrame.Cursor = Cursors.ScrollAll;
        e.Handled = true;
    }

    private void Map_MouseMove(object sender, MouseEventArgs e)
    {
        if (_panStart is not Point start) return;
        if (e.LeftButton != MouseButtonState.Pressed) { EndPan(); return; }
        var point = e.GetPosition(MapScroll);
        MapScroll.ScrollToHorizontalOffset(_panOffset.X + start.X - point.X);
        MapScroll.ScrollToVerticalOffset(_panOffset.Y + start.Y - point.Y);
        e.Handled = true;
    }

    private void Map_MouseUp(object sender, MouseButtonEventArgs e)
    {
        if (_panStart == null) return;
        EndPan();
        e.Handled = true;
    }

    private void Map_LostCapture(object sender, MouseEventArgs e) => EndPan();

    private void Map_KeyDown(object sender, KeyEventArgs e)
    {
        if (e.Key != Key.Escape || _panStart == null) return;
        MapScroll.ScrollToHorizontalOffset(_panOffset.X);
        MapScroll.ScrollToVerticalOffset(_panOffset.Y);
        EndPan();
        e.Handled = true;
    }

    private void EndPan()
    {
        if (_panStart == null) return;
        _panStart = null;
        MapFrame.ReleaseMouseCapture();
        MapFrame.Cursor = Cursors.Hand;
    }

    private void Node_MouseDown(object sender, MouseButtonEventArgs e)
    {
        if (sender is not Border { DataContext: NetworkNode node } border || Model == null) return;
        border.Focus();
        var point = e.GetPosition(MapCanvas);
        _offset = new Point(point.X - node.X, point.Y - node.Y);
        _start = new Point(node.X, node.Y);
        _dragged = border;
        if (NodeItems.ItemContainerGenerator.ContainerFromItem(node) is UIElement container)
            Panel.SetZIndex(container, ++_zIndex);
        border.CaptureMouse();
        e.Handled = true;
    }

    private void Node_MouseMove(object sender, MouseEventArgs e)
    {
        if (_dragged?.DataContext is not NetworkNode node || Model == null) return;
        if (e.LeftButton != MouseButtonState.Pressed) { EndDrag(); return; }
        var point = e.GetPosition(MapCanvas);
        Model.Move(node, point.X - _offset.X, point.Y - _offset.Y);
        var viewportPoint = e.GetPosition(MapScroll);
        if (viewportPoint.X < 24) MapScroll.ScrollToHorizontalOffset(MapScroll.HorizontalOffset - 20);
        else if (viewportPoint.X > MapScroll.ViewportWidth - 24) MapScroll.ScrollToHorizontalOffset(MapScroll.HorizontalOffset + 20);
        if (viewportPoint.Y < 24) MapScroll.ScrollToVerticalOffset(MapScroll.VerticalOffset - 20);
        else if (viewportPoint.Y > MapScroll.ViewportHeight - 24) MapScroll.ScrollToVerticalOffset(MapScroll.VerticalOffset + 20);
        e.Handled = true;
    }

    private void Node_MouseUp(object sender, MouseButtonEventArgs e) { EndDrag(); e.Handled = true; }
    private void Node_LostCapture(object sender, MouseEventArgs e) => EndDrag();

    private void EndDrag()
    {
        if (_dragged == null) return;
        var border = _dragged;
        _dragged = null;
        border.ReleaseMouseCapture();
        Model?.SaveLayout();
    }

    private void Node_KeyDown(object sender, KeyEventArgs e)
    {
        if (sender is not Border { DataContext: NetworkNode node } || Model == null) return;
        if (e.Key == Key.Escape && _dragged != null)
        {
            Model.Move(node, _start.X, _start.Y);
            EndDrag();
            e.Handled = true;
            return;
        }
        double step = Keyboard.Modifiers.HasFlag(ModifierKeys.Shift) ? 1 : 10;
        var (dx, dy) = e.Key switch
        {
            Key.Left => (-step, 0d), Key.Right => (step, 0d),
            Key.Up => (0d, -step), Key.Down => (0d, step), _ => (0d, 0d)
        };
        if (dx == 0 && dy == 0) return;
        Model.Move(node, node.X + dx, node.Y + dy);
        Model.SaveLayout();
        e.Handled = true;
    }

    private void Fit_Click(object sender, RoutedEventArgs e)
    {
        if (Model == null) return;
        Zoom = Math.Clamp(Math.Min(MapScroll.ViewportWidth / Model.CanvasWidth,
            MapScroll.ViewportHeight / Model.CanvasHeight), 0.1, 1);
        MapScroll.ScrollToHorizontalOffset(MapFrame.Padding.Left - Math.Max(0, (MapScroll.ViewportWidth - Model.CanvasWidth * Zoom) / 2));
        MapScroll.ScrollToVerticalOffset(MapFrame.Padding.Top - Math.Max(0, (MapScroll.ViewportHeight - Model.CanvasHeight * Zoom) / 2));
    }

    private void ActualSize_Click(object sender, RoutedEventArgs e) => Zoom = 1;
    private void Reset_Click(object sender, RoutedEventArgs e) => Model?.ResetLayout();
}
