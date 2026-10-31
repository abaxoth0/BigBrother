using System.Runtime.InteropServices;
using System.Windows;
using System.Windows.Interop;
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
        CheckWindowCornerRegion(window);
        var bitmap = RenderWindowFrame(window);
        foreach (var (x, y) in new[]
        {
            (bitmap.PixelWidth / 2, 0), (bitmap.PixelWidth / 2, bitmap.PixelHeight - 1),
            (0, bitmap.PixelHeight / 2), (bitmap.PixelWidth - 1, bitmap.PixelHeight / 2),
            (0, 0), (bitmap.PixelWidth - 1, 0),
            (0, bitmap.PixelHeight - 1), (bitmap.PixelWidth - 1, bitmap.PixelHeight - 1)
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

    private static void CheckWindowCornerRegion(Window window)
    {
        var handle = new WindowInteropHelper(window).Handle;
        Check(GetWindowRect(handle, out var bounds), "The native window bounds must be available");
        var region = CreateRectRgn(0, 0, 0, 0);
        try
        {
            Check(region != IntPtr.Zero && GetWindowRgn(handle, region) != 0,
                "Custom chrome must have an explicit window region");
            foreach (var (x, y) in new[]
            {
                (0, 0), (bounds.Right - bounds.Left - 1, 0),
                (0, bounds.Bottom - bounds.Top - 1),
                (bounds.Right - bounds.Left - 1, bounds.Bottom - bounds.Top - 1)
            })
                Check(PtInRegion(region, x, y),
                    $"{window.GetType().Name}: native rounding must not clip the border at ({x}, {y})");
        }
        finally
        {
            if (region != IntPtr.Zero) DeleteObject(region);
        }
    }

    [StructLayout(LayoutKind.Sequential)]
    private struct WindowFrameRect { public int Left, Top, Right, Bottom; }

    [DllImport("user32.dll")]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool GetWindowRect(IntPtr window, out WindowFrameRect bounds);

    [DllImport("user32.dll")]
    private static extern int GetWindowRgn(IntPtr window, IntPtr region);

    [DllImport("gdi32.dll")]
    private static extern IntPtr CreateRectRgn(int left, int top, int right, int bottom);

    [DllImport("gdi32.dll")]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool PtInRegion(IntPtr region, int x, int y);

    [DllImport("gdi32.dll")]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool DeleteObject(IntPtr handle);

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
