using System.IO;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using System.Windows.Shell;
using frontend;
using frontend.Models;
using frontend.Views;

internal static partial class Program
{
    private sealed class FrameOwnerWindow : ChromeWindow
    {
        public FrameOwnerWindow()
        {
            Width = 820;
            Height = 580;
            WindowStyle = WindowStyle.None;
            Content = new TextBlock { Text = "Белые списки", Margin = new Thickness(20) };
            SetResourceReference(BackgroundProperty, "WindowBackground");
            SetResourceReference(ForegroundProperty, "TitleBarText");
            WindowChrome.SetWindowChrome(this, new WindowChrome
            {
                CaptionHeight = 42, ResizeBorderThickness = new Thickness(6),
                GlassFrameThickness = new Thickness(0), UseAeroCaptionButtons = false,
                CornerRadius = new CornerRadius(8)
            });
        }
    }

    private static void TestWindowFrames()
    {
        var owner = new FrameOwnerWindow();
        var dialog = new WhitelistPresetWindow(Array.Empty<WhitelistPreset>(),
            new WhitelistPreset { ActiveWhitelists = new() { "common", "yandex" } },
            (_, _) => Task.CompletedTask);
        var domain = new DomainCheckWindow(Array.Empty<ComposedWhitelistEntry>());
        WindowChrome.GetWindowChrome(domain).CornerRadius = new CornerRadius(8);
        int previousText = AppearanceSettings.Current.TextFontSize;
        int previousInterface = AppearanceSettings.Current.InterfaceFontSize;
        try
        {
            AppearanceSettings.Current.TextFontSize = 14;
            AppearanceSettings.Current.InterfaceFontSize = 14;
            owner.Show();
            dialog.Owner = owner;
            dialog.Show();
            domain.Owner = owner;
            domain.Show();
            FlushLayout(owner);
            FlushLayout(dialog);
            FlushLayout(domain);
            CheckWindowFrame(owner);
            CheckWindowFrame(dialog);
            CheckWindowFrame(domain);
            Check(dialog.ResizeMode == ResizeMode.NoResize && dialog.SizeToContent == SizeToContent.Height,
                "Adding a frame must preserve the preset dialog's sizing behaviour");
            owner.Width += 80;
            owner.Height += 60;
            owner.Activate();
            FlushLayout(owner);
            CheckWindowFrame(owner);
            CheckWindowFrame(dialog);
            domain.Width += 80;
            domain.Height += 60;
            FlushLayout(domain);
            CheckWindowFrame(domain);
            dialog.Activate();
            FlushLayout(dialog);
            CheckWindowFrame(dialog);

            if (Environment.GetEnvironmentVariable("BIGBROTHER_WINDOW_FRAME_SCREENSHOT") is { Length: > 0 } path)
            {
                var ownerImage = RenderWindowFrame(owner);
                var dialogImage = RenderWindowFrame(dialog);
                var scene = new DrawingVisual();
                using (var drawing = scene.RenderOpen())
                {
                    drawing.DrawImage(ownerImage, new Rect(0, 0, ownerImage.Width, ownerImage.Height));
                    drawing.DrawImage(dialogImage, new Rect(100, 80, dialogImage.Width, dialogImage.Height));
                }
                var bitmap = new RenderTargetBitmap(ownerImage.PixelWidth, ownerImage.PixelHeight,
                    96, 96, PixelFormats.Pbgra32);
                bitmap.Render(scene);
                var encoder = new PngBitmapEncoder();
                encoder.Frames.Add(BitmapFrame.Create(bitmap));
                using var file = File.Create(path);
                encoder.Save(file);
                var domainEncoder = new PngBitmapEncoder();
                domainEncoder.Frames.Add(BitmapFrame.Create(RenderWindowFrame(domain)));
                using var domainFile = File.Create(Path.ChangeExtension(path, "domain.png"));
                domainEncoder.Save(domainFile);
            }
            Console.WriteLine("PASS: visible borders and unclipped native corners, preset/domain dialogs, focus changes and resizing");
        }
        finally
        {
            domain.Close();
            dialog.Close();
            owner.Close();
            AppearanceSettings.Current.TextFontSize = previousText;
            AppearanceSettings.Current.InterfaceFontSize = previousInterface;
        }
    }
}
