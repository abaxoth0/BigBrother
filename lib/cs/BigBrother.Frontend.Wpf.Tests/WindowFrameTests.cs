using System.Windows;
using System.Windows.Media;
using System.Windows.Media.Imaging;

internal static partial class Program
{
    private static BitmapSource RenderWindowFrame(Window window)
    {
        window.UpdateLayout();
        var root = (FrameworkElement)VisualTreeHelper.GetChild(window, 0);
        var bitmap = new RenderTargetBitmap((int)Math.Round(root.ActualWidth),
            (int)Math.Round(root.ActualHeight), 96, 96, PixelFormats.Pbgra32);
        bitmap.Render(root);
        return bitmap;
    }

    private static void CheckWindowFrame(Window window)
    {
        var thickness = window.BorderThickness;
        Check(thickness.Left is >= 1 and <= 2 && thickness.Top == thickness.Left &&
              thickness.Right == thickness.Left && thickness.Bottom == thickness.Left,
            "Custom windows must have a thin frame on all four sides");
        Check(window.BorderBrush is SolidColorBrush && window.Background is SolidColorBrush,
            "The window frame and background must use theme colours");
        var frame = ((SolidColorBrush)window.BorderBrush).Color;
        var background = ((SolidColorBrush)window.Background).Color;
        double contrast = (Luminance(frame) + 0.05) / (Luminance(background) + 0.05);
        Check(contrast >= 1.5, "Window outlines must stand out against the dark background");
        var bitmap = RenderWindowFrame(window);
        foreach (var (x, y) in new[]
        {
            (bitmap.PixelWidth / 2, 0), (bitmap.PixelWidth / 2, bitmap.PixelHeight - 1),
            (0, bitmap.PixelHeight / 2), (bitmap.PixelWidth - 1, bitmap.PixelHeight / 2)
        })
        {
            var pixel = new byte[4];
            bitmap.CopyPixels(new Int32Rect(x, y, 1, 1), pixel, 4, 0);
            Check(Math.Abs(pixel[0] - frame.B) <= 2 && Math.Abs(pixel[1] - frame.G) <= 2 &&
                  Math.Abs(pixel[2] - frame.R) <= 2 && pixel[3] == 255,
                $"{window.GetType().Name}: expected {frame} at ({x}, {y}), got BGRA {string.Join(",", pixel)} " +
                $"on {VisualTreeHelper.GetChild(window, 0).GetType().Name} ({bitmap.PixelWidth} x {bitmap.PixelHeight})");
        }
    }

    private static double Luminance(Color colour)
    {
        static double Linear(byte value)
        {
            double normalized = value / 255.0;
            return normalized <= 0.04045 ? normalized / 12.92 : Math.Pow((normalized + 0.055) / 1.055, 2.4);
        }
        return 0.2126 * Linear(colour.R) + 0.7152 * Linear(colour.G) + 0.0722 * Linear(colour.B);
    }
}
