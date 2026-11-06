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
    private int _zIndex;
    private NetworkSchemeViewModel? Model => DataContext as NetworkSchemeViewModel;

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
        ZoomSlider.Value = Math.Clamp(Math.Min(MapScroll.ViewportWidth / Model.CanvasWidth,
            MapScroll.ViewportHeight / Model.CanvasHeight), ZoomSlider.Minimum, 1);
        MapScroll.ScrollToHome();
    }

    private void ActualSize_Click(object sender, RoutedEventArgs e) => ZoomSlider.Value = 1;
    private void Reset_Click(object sender, RoutedEventArgs e) => Model?.ResetLayout();
}
