using System.IO;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using System.Windows.Threading;
using frontend;
using frontend.Models;
using frontend.ViewModels;
using frontend.Views;

internal static partial class Program
{
    private sealed class WhitelistToolbarState : ViewModelBase
    {
        private bool _changed;
        private int _index;
        public List<string> Executed { get; } = new();
        public string ActiveWhitelistsText => "Активных списков: 3";
        public string ActiveTemporaryWhitelistsText => "Активных временных списков: 1";
        public bool CanUseWhitelistPresets => true;
        public bool IsAllSelected { get; set; }
        public int WhitelistTableIndex { get => _index; set => SetProperty(ref _index, value); }
        public bool HasWhitelistActivationChanges
        {
            get => _changed;
            set
            {
                if (SetProperty(ref _changed, value)) CommandManager.InvalidateRequerySuggested();
            }
        }
        public ICommand OpenCreateWhitelistCommand => Command("create");
        public ICommand ImportWhitelistsCommand => Command("import");
        public ICommand ExportWhitelistsCommand => Command("export");
        public ICommand SaveWhitelistPresetCommand => Command("preset");
        public ICommand OpenDomainCheckCommand => Command("check");
        public ICommand SyncWhitelistsCommand => Command("sync");
        public ICommand SaveWhitelistActivationCommand => new RelayCommand(_ =>
        {
            Executed.Add("save");
            HasWhitelistActivationChanges = false;
        }, _ => HasWhitelistActivationChanges);
        public ICommand CancelWhitelistActivationCommand => new RelayCommand(_ =>
        {
            Executed.Add("cancel");
            HasWhitelistActivationChanges = false;
        }, _ => HasWhitelistActivationChanges);
        private ICommand Command(string name) => new RelayCommand(_ => Executed.Add(name));
    }

    private static void TestWhitelistToolbar()
    {
        var state = new WhitelistToolbarState();
        var toolbar = new WhitelistToolbar();
        var activation = new WhitelistActivationBar();
        var lists = new WhitelistTable
        {
            Margin = new Thickness(0, 6, 0, 0),
            ItemsSource = new[]
            {
                new WhitelistInfo { Name = "programming", EntryCount = 18, IsActive = true },
                new WhitelistInfo { Name = "ports", EntryCount = 6, IsActive = true },
                new WhitelistInfo { Name = "priorities", EntryCount = 12, IsActive = true }
            }
        };
        var root = new Grid { Margin = new Thickness(8), DataContext = state, Width = 850, Height = 900 };
        root.SetResourceReference(Panel.BackgroundProperty, "WindowBackground");
        root.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });
        root.RowDefinitions.Add(new RowDefinition { Height = new GridLength(1, GridUnitType.Star) });
        root.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });
        Grid.SetRow(lists, 1);
        Grid.SetRow(activation, 2);
        root.Children.Add(toolbar);
        root.Children.Add(lists);
        root.Children.Add(activation);
        var window = new Window { Content = root, SizeToContent = SizeToContent.WidthAndHeight };
        window.SetResourceReference(Control.FontSizeProperty, "FontSize14");
        window.SetResourceReference(Control.ForegroundProperty, "TitleBarText");
        window.SetResourceReference(Control.BackgroundProperty, "WindowBackground");
        var pending = (Border)activation.FindName("PendingChangesPanel");
        int previousSize = AppearanceSettings.Current.InterfaceFontSize;
        int previousTextSize = AppearanceSettings.Current.TextFontSize;
        try
        {
            window.Show();
            foreach (int size in new[] { 10, 14, 20, 28 })
            foreach (int width in new[] { 500, 850, 1200 })
            {
                AppearanceSettings.Current.InterfaceFontSize = size;
                root.Width = width;
                foreach (bool changed in new[] { false, true })
                {
                    state.HasWhitelistActivationChanges = changed;
                    FlushLayout(window);
                    Check(Math.Abs(root.ActualWidth - width) < 1, "The test viewport must use the requested width");
                    Check(pending.Visibility == (changed ? Visibility.Visible : Visibility.Collapsed),
                        "Activation actions must appear only while changes are pending");
                    CheckControlsFit(toolbar, $"toolbar at {width}px / {size}px font");
                    if (changed) CheckControlsFit(activation, $"activation bar at {width}px / {size}px font");
                }
            }

            var selector = (ComboBox)toolbar.FindName("ListTypeSelector");
            selector.SelectedIndex = 1;
            Check(state.WhitelistTableIndex == 1, "The table selector must still update the view model");
            selector.SelectedIndex = 0;
            Check(state.WhitelistTableIndex == 0, "The table selector must still switch back");
            foreach (var (name, action) in new[]
            {
                ("CreateWhitelistButton", "create"), ("ImportWhitelistsButton", "import"),
                ("ExportWhitelistsButton", "export"), ("DomainCheckButton", "check"),
                ("SyncWhitelistsButton", "sync")
            })
            {
                var button = (Button)toolbar.FindName(name);
                Check(button.Command?.CanExecute(null) == true, "A toolbar action lost its command binding");
                button.Command!.Execute(null);
                Check(state.Executed.Last() == action, "A toolbar action invoked the wrong command");
            }
            var presets = (WhitelistPresetButtons)toolbar.FindName("Presets");
            ((Button)presets.FindName("SaveButton")).Command.Execute(null);
            Check(state.Executed.Last() == "preset", "Preset saving must retain its command binding");
            foreach (var (name, action) in new[] { ("SaveActivationButton", "save"), ("CancelActivationButton", "cancel") })
            {
                state.HasWhitelistActivationChanges = true;
                FlushLayout(window);
                var button = (Button)activation.FindName(name);
                Check(button.IsEnabled, "Pending activation actions must be enabled");
                button.Command.Execute(null);
                FlushLayout(window);
                Check(state.Executed.Last() == action && pending.Visibility == Visibility.Collapsed,
                    "Saving or cancelling must dismiss the pending changes bar");
            }

            if (Environment.GetEnvironmentVariable("BIGBROTHER_WL_SCREENSHOT") is { Length: > 0 } path)
            {
                AppearanceSettings.Current.InterfaceFontSize = 14;
                AppearanceSettings.Current.TextFontSize = 14;
                root.Width = 1100;
                root.Height = 660;
                state.HasWhitelistActivationChanges = true;
                FlushLayout(window);
                var bitmap = new RenderTargetBitmap((int)root.ActualWidth, (int)root.ActualHeight,
                    96, 96, PixelFormats.Pbgra32);
                bitmap.Render(root);
                var encoder = new PngBitmapEncoder();
                encoder.Frames.Add(BitmapFrame.Create(bitmap));
                using var file = File.Create(path);
                encoder.Save(file);
            }
            Console.WriteLine("PASS: whitelist toolbar wrapping at four font sizes and three widths, commands and pending activation actions");
        }
        finally
        {
            window.Close();
            AppearanceSettings.Current.InterfaceFontSize = previousSize;
            AppearanceSettings.Current.TextFontSize = previousTextSize;
        }
    }

    private static void FlushLayout(Window window)
    {
        window.UpdateLayout();
        Dispatcher.CurrentDispatcher.Invoke(() => { }, DispatcherPriority.ApplicationIdle);
        window.UpdateLayout();
    }

    private static void CheckControlsFit(FrameworkElement area, string message)
    {
        var buttons = VisualChildren<Button>(area).Where(button => button.IsVisible).ToList();
        Check(buttons.Count > 0, "The layout check requires visible actions in the " + message);
        var bounds = buttons.Select(button => button.TransformToAncestor(area)
            .TransformBounds(new Rect(button.RenderSize))).ToList();
        Check(bounds.All(rect => rect.Left >= -1 && rect.Top >= -1 &&
              rect.Right <= area.ActualWidth + 1 && rect.Bottom <= area.ActualHeight + 1),
            "Controls must stay inside the " + message);
        for (int index = 0; index < bounds.Count; index++)
        for (int other = index + 1; other < bounds.Count; other++)
        {
            var intersection = Rect.Intersect(bounds[index], bounds[other]);
            Check(intersection.IsEmpty || intersection.Width < 1 || intersection.Height < 1,
                "Controls must not overlap in the " + message);
        }
    }

    private static IEnumerable<T> VisualChildren<T>(DependencyObject parent) where T : DependencyObject
    {
        for (int index = 0; index < VisualTreeHelper.GetChildrenCount(parent); index++)
        {
            var child = VisualTreeHelper.GetChild(parent, index);
            if (child is T value) yield return value;
            foreach (var descendant in VisualChildren<T>(child)) yield return descendant;
        }
    }
}
