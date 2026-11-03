using System.Windows;
using System.Windows.Controls;
using System.Windows.Data;
using System.Windows.Input;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using System.Windows.Threading;
using frontend;
using frontend.ViewModels;
using frontend.Views;
using frontend.Services;

internal static partial class Program
{
    private sealed class SettingsState : ViewModelBase
    {
        private string _serverName = "Test server", _serverAddress = "192.0.2.1";
        private bool _changed, _connected = true, _notifications = true;
        public int Saves { get; private set; }
        public ICommand SaveSettingsCommand { get; }
        public ICommand SaveServerSettingsCommand => SaveSettingsCommand;
        public SettingsState() => SaveSettingsCommand = new RelayCommand(_ => { Saves++; HasSettingsChanges = false; });
        public bool HasSettingsChanges { get => _changed; set => SetProperty(ref _changed, value); }
        public string ServerName { get => _serverName; set { if (SetProperty(ref _serverName, value)) HasSettingsChanges = true; } }
        public string ServerAddress { get => _serverAddress; set { if (SetProperty(ref _serverAddress, value)) HasSettingsChanges = true; } }
        public bool WindowsNotificationsEnabled { get => _notifications; set { if (SetProperty(ref _notifications, value)) HasSettingsChanges = true; } }
        public bool IsConnected
        {
            get => _connected;
            set
            {
                if (!SetProperty(ref _connected, value)) return;
                OnPropertyChanged(nameof(SettingsAvailable));
                OnPropertyChanged(nameof(ConnectedVisibility));
                OnPropertyChanged(nameof(NotConnectedVisibility));
            }
        }
        public bool SettingsAvailable => IsConnected;
        public Visibility ConnectedVisibility => IsConnected ? Visibility.Visible : Visibility.Collapsed;
        public Visibility NotConnectedVisibility => IsConnected ? Visibility.Collapsed : Visibility.Visible;
        public string ServerPort { get; set; } = "1984";
        public string Username { get; set; } = "Test client";
        public bool CanRegister => true;
        public bool DiscoveryEnabled { get; set; } = true;
        public bool NetworkAuto { get; set; } = true;
        public bool NetworkManualMode => !NetworkAuto;
        public string NetworkGateway { get; set; } = "192.0.2.254";
        public string NetworkMask { get; set; } = "255.255.255.0";
        public bool FallbackWhitelistEnabled { get; set; }
        public bool FiltrationAutoDisable { get; set; } = true;
        public bool Ipv6BlockEnabled { get; set; } = true;
        public bool SaveTemporaryWhitelistState { get; set; }
        public string CertificateFingerprint => new('a', 64);
        public string PinnedCertificateFingerprint => CertificateFingerprint;
        public string ObservedCertificateFingerprint => "";
        public string VerifiedCertificateFingerprint { get; set; } = "";
        public string CertificateEndpoint => "192.0.2.1:1984";
        public string CertificateStatus => "";
        public string ClientUpdateLocation { get; set; } = "";
        public string ClientUpdateStatus => "";
        private ICommand Action { get; } = new RelayCommand(_ => { });
        public ICommand RegisterCommand => Action;
        public ICommand ChangeServerCommand => Action;
        public ICommand InspectCertificateCommand => Action;
        public ICommand ApplyCertificateFingerprintCommand => Action;
        public ICommand CopyCertificateFingerprintCommand => Action;
        public ICommand BrowseClientUpdateCommand => Action;
        public ICommand OpenUpdateShareCommand => Action;
        public ICommand ClearClientUpdateCommand => Action;
        public ICommand PublishClientUpdateCommand => Action;
    }

    private static void TestSettingsNavigation(bool server)
    {
        var expected = server
            ? new[] { ("Сервер", new[] { "Сервер", "Сертификат сервера", "Временные списки" }),
                ("Обновления", new[] { "Обновления клиентов" }), ("Внешний вид", new[] { "Внешний вид" }),
                ("Прочее", new[] { "Уведомления" }) }
            : new[] { ("Фильтрация", new[] { "Фильтрация", "Белый список" }),
                ("Сервер", new[] { "Сервер", "Обнаружение сервера", "Сертификат сервера" }),
                ("Сеть", new[] { "Сеть" }), ("Внешний вид", new[] { "Внешний вид" }),
                ("Прочее", new[] { "Уведомления" }) };
        var state = new SettingsState();
        var view = new SettingsView { DataContext = state };
        var browser = (SettingsBrowser)view.FindName("Settings");
        view.SetResourceReference(Control.BackgroundProperty, "WindowBackground");
        var window = new Window { Content = view, Width = 1000, Height = 900 };
        window.SetResourceReference(Control.FontSizeProperty, "FontSize14");
        window.SetResourceReference(Control.ForegroundProperty, "TitleBarText");
        window.SetResourceReference(Control.BackgroundProperty, "WindowBackground");
        int previousSize = AppearanceSettings.Current.InterfaceFontSize;
        int previousTextSize = AppearanceSettings.Current.TextFontSize;
        string testApplication = "SettingsTest-" + Guid.NewGuid().ToString("N");
        string appearanceDirectory = System.IO.Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
            "BigBrother", testApplication);
        string appearancePath = System.IO.Path.Combine(appearanceDirectory, "appearance.json");
        try
        {
            AppearanceSettings.Current.Initialize(testApplication);
            AppearanceSettings.Current.Save();
            foreach (var category in browser.Categories)
            foreach (var field in SettingsLogicalElements<TextBox>(category.Content))
                if (field.FontFamily.Source == "Consolas" && !Fonts.SystemFontFamilies.Any(font => font.Source == "Consolas"))
                    field.FontFamily = new FontFamily("Liberation Mono");
            window.Show();
            SettingsLayout(window);
            Check(browser.Categories.Select(category => category.Title).SequenceEqual(expected.Select(item => item.Item1)),
                "Settings must show the requested categories in order");
            Check(browser.SelectedCategory == null && !SettingsVisuals<GroupBox>(browser).Any(group => group.IsVisible),
                "Opening Settings must show only the category overview");
            var cards = SettingsVisuals<Button>(browser).Where(button => button.Tag is SettingsCategory).ToArray();
            var back = (Button)browser.FindName("BackButton");
            var save = (Button)browser.FindName("SaveButton");
            var pendingHint = (TextBlock)browser.FindName("PendingHint");
            Check(save.IsVisible && !save.IsEnabled && !pendingHint.IsVisible,
                "The category overview must offer the common Save button even without pending changes");
            void Open(string title)
            {
                if (browser.SelectedCategory != null)
                    back.RaiseEvent(new RoutedEventArgs(Button.ClickEvent));
                cards.Single(button => ((SettingsCategory)button.Tag).Title == title)
                    .RaiseEvent(new RoutedEventArgs(Button.ClickEvent));
                SettingsLayout(window);
            }
            foreach (var (title, headers) in expected)
            {
                Open(title);
                Check(SettingsVisuals<GroupBox>(browser).Where(group => group.IsVisible)
                    .Select(group => group.Header.ToString()).SequenceEqual(headers),
                    $"{title} must contain exactly its own settings sections");
                Check(cards.All(card => !card.IsVisible) && back.IsVisible,
                    "A category must replace the overview and offer a return action");
                Check(save.IsVisible && SettingsVisuals<Button>(browser).Count(button => button.IsVisible &&
                    button.Content is string text && text.StartsWith("Сохранить")) == 1,
                    "Every category, including Appearance, must have exactly one settings Save button");
                Check(SettingsVisuals<Button>(browser).Where(button => button.IsVisible &&
                    BindingOperations.GetBinding(button, Button.CommandProperty) != null).All(button => button.Command != null),
                    "Category actions must retain their command bindings");
                back.RaiseEvent(new RoutedEventArgs(Button.ClickEvent));
                SettingsLayout(window);
                Check(browser.SelectedCategory == null && cards.All(card => card.IsVisible),
                    "Back must return to the category overview");
            }

            Open("Сервер");
            string path = server ? "ServerName" : "ServerAddress";
            var input = SettingsVisuals<TextBox>(browser).Single(field => field.IsVisible &&
                BindingOperations.GetBinding(field, TextBox.TextProperty)?.Path.Path == path);
            input.SetCurrentValue(TextBox.TextProperty, "draft.example");
            SettingsLayout(window);
            Check(state.HasSettingsChanges && save.IsEnabled, "Category fields must retain their editable bindings and save command");
            back.RaiseEvent(new RoutedEventArgs(Button.ClickEvent));
            SettingsLayout(window);
            Check(save.IsVisible && save.IsEnabled && pendingHint.IsVisible,
                "Pending backend edits must be saveable directly from the category overview");
            Open("Прочее");
            var notifications = SettingsVisuals<CheckBox>(browser).Single(box => box.IsVisible);
            notifications.IsChecked = false;
            Open("Сервер");
            Check(input.Text == "draft.example" && !state.WindowsNotificationsEnabled,
                "Switching categories must retain drafts in every category");
            state.IsConnected = false;
            SettingsLayout(window);
            Check(!save.IsEnabled && !input.IsVisible, "Backend settings and saving must be unavailable after disconnect");
            state.IsConnected = true;
            SettingsLayout(window);
            Check(save.IsEnabled && input.Text == "draft.example", "Reconnect must preserve category drafts");
            back.RaiseEvent(new RoutedEventArgs(Button.ClickEvent));
            save.Command.Execute(null);
            SettingsLayout(window);
            Check(state.Saves == 1 && !save.IsEnabled && !state.HasSettingsChanges,
                "Save must invoke the existing settings command and clear the pending state");

            Open("Сервер");
            browser.RaiseEvent(new KeyEventArgs(Keyboard.PrimaryDevice, PresentationSource.FromVisual(window)!, 0, Key.Escape)
                { RoutedEvent = Keyboard.KeyDownEvent });
            Check(browser.SelectedCategory == null, "Escape must return to the categories");
            Open("Внешний вид");
            var appearance = (AppearanceEditor)browser.SelectedCategory!.Content;
            var fontInput = (TextBox)appearance.FindName("InterfaceFontSizeInput");
            fontInput.SetCurrentValue(TextBox.TextProperty, "99");
            fontInput.RaiseEvent(new KeyEventArgs(Keyboard.PrimaryDevice, PresentationSource.FromVisual(window)!, 0, Key.Escape)
                { RoutedEvent = Keyboard.PreviewKeyDownEvent });
            Check(browser.SelectedCategory?.Title == "Внешний вид" && fontInput.Text == "14",
                "Escape inside a font input must still cancel the edit before navigating back");
            fontInput.SetCurrentValue(TextBox.TextProperty, "16");
            SettingsLayout(window);
            Check(save.IsEnabled && pendingHint.IsVisible,
                "Typing a font size must enable the common Save button before focus leaves the input");
            fontInput.RaiseEvent(new KeyEventArgs(Keyboard.PrimaryDevice, PresentationSource.FromVisual(window)!, 0, Key.Enter)
                { RoutedEvent = Keyboard.PreviewKeyDownEvent });
            Check(window.FontSize == 16 && AppearanceSettings.Current.HasChanges &&
                  AppearancePreferences.Load(appearancePath).InterfaceFontSize == 14,
                "Font changes must preview immediately while persistence waits for the common Save button");
            var textInput = (TextBox)appearance.FindName("TextFontSizeInput");
            textInput.SetCurrentValue(TextBox.TextProperty, "18");
            back.RaiseEvent(new RoutedEventArgs(Button.ClickEvent));
            SettingsLayout(window);
            Check(save.IsVisible && save.IsEnabled && pendingHint.IsVisible,
                "Pending font edits must remain saveable from the category overview");
            save.Command.Execute(null);
            SettingsLayout(window);
            var saved = AppearancePreferences.Load(appearancePath);
            Check(saved.TextFontSize == 18 && saved.InterfaceFontSize == 16 && !AppearanceSettings.Current.HasChanges &&
                  !save.IsEnabled && !pendingHint.IsVisible && state.Saves == 1,
                "The common Save must commit both font inputs and persist them without issuing an unnecessary backend save");
            Open("Сервер");
            input.SetCurrentValue(TextBox.TextProperty, "combined.example");
            AppearanceSettings.Current.InterfaceFontSize = 17;
            back.RaiseEvent(new RoutedEventArgs(Button.ClickEvent));
            save.Command.Execute(null);
            SettingsLayout(window);
            Check(state.Saves == 2 && !state.HasSettingsChanges && !AppearanceSettings.Current.HasChanges &&
                  AppearancePreferences.Load(appearancePath).InterfaceFontSize == 17 && !save.IsEnabled,
                "One Save action must persist combined backend and appearance edits");
            AppearanceSettings.Current.InterfaceFontSize = 18;
            AppearanceSettings.Current.InterfaceFontSize = 17;
            SettingsLayout(window);
            Check(!save.IsEnabled && !pendingHint.IsVisible,
                "Reverting font sizes to their saved values must clear the pending state");
            AppearanceSettings.Current.InterfaceFontSize = 18;
            System.IO.File.Delete(appearancePath);
            System.IO.Directory.CreateDirectory(appearancePath);
            save.Command.Execute(null);
            SettingsLayout(window);
            Check(AppearanceSettings.Current.HasChanges && save.IsEnabled && pendingHint.IsVisible &&
                  AppearanceSettings.Current.Status.StartsWith("Не удалось"),
                "A failed appearance save must retain pending edits and expose the failure from the overview");
            System.IO.Directory.Delete(appearancePath);
            save.Command.Execute(null);
            SettingsLayout(window);
            Check(!AppearanceSettings.Current.HasChanges && !save.IsEnabled &&
                  AppearancePreferences.Load(appearancePath).InterfaceFontSize == 18,
                "Retrying the common Save must persist the retained font edit");
            AppearanceSettings.Current.InterfaceFontSize = 14;
            AppearanceSettings.Current.TextFontSize = 14;
            save.Command.Execute(null);
            Open("Внешний вид");
            view.Visibility = Visibility.Collapsed;
            view.Visibility = Visibility.Visible;
            SettingsLayout(window);
            Check(browser.SelectedCategory == null, "Reopening Settings must reset to the category overview");
            foreach (int size in new[] { 10, 14, 20, 28 })
            foreach (int width in new[] { 500, 1000 })
            {
                AppearanceSettings.Current.InterfaceFontSize = size;
                window.Width = width;
                SettingsLayout(window);
                Check(cards.All(card => card.ActualWidth > 0 && card.ActualWidth <= browser.ActualWidth),
                    "Category cards must fit the available width at every supported font size");
            }
            if (Environment.GetEnvironmentVariable("BIGBROTHER_SETTINGS_SCREENSHOT") is { Length: > 0 } screenshot)
            {
                AppearanceSettings.Current.InterfaceFontSize = 14;
                window.Width = 1000;
                SettingsLayout(window);
                SaveSettingsScreenshot(view, screenshot);
                Open("Сервер");
                SaveSettingsScreenshot(view, System.IO.Path.ChangeExtension(screenshot, "server.png"));
            }
            Console.WriteLine("PASS: settings category mappings, back/Escape/reentry, cross-category drafts, saving, disconnect and font scaling");
        }
        finally
        {
            window.Close();
            AppearanceSettings.Current.InterfaceFontSize = previousSize;
            AppearanceSettings.Current.TextFontSize = previousTextSize;
            if (System.IO.Directory.Exists(appearanceDirectory)) System.IO.Directory.Delete(appearanceDirectory, recursive: true);
        }
    }

    private static void SettingsLayout(Window window)
    {
        window.UpdateLayout();
        Dispatcher.CurrentDispatcher.Invoke(() => { }, DispatcherPriority.ApplicationIdle);
        window.UpdateLayout();
    }

    private static IEnumerable<T> SettingsVisuals<T>(DependencyObject parent) where T : DependencyObject
    {
        for (int index = 0; index < VisualTreeHelper.GetChildrenCount(parent); index++)
        {
            var child = VisualTreeHelper.GetChild(parent, index);
            if (child is T value) yield return value;
            foreach (var descendant in SettingsVisuals<T>(child)) yield return descendant;
        }
    }

    private static IEnumerable<T> SettingsLogicalElements<T>(DependencyObject parent) where T : DependencyObject
    {
        if (parent is T value) yield return value;
        foreach (var child in LogicalTreeHelper.GetChildren(parent).OfType<DependencyObject>())
            foreach (var descendant in SettingsLogicalElements<T>(child)) yield return descendant;
    }

    private static void SaveSettingsScreenshot(FrameworkElement view, string path)
    {
        var bitmap = new RenderTargetBitmap((int)Math.Round(view.ActualWidth), (int)Math.Round(view.ActualHeight),
            96, 96, PixelFormats.Pbgra32);
        bitmap.Render(view);
        var encoder = new PngBitmapEncoder();
        encoder.Frames.Add(BitmapFrame.Create(bitmap));
        using var file = System.IO.File.Create(path);
        encoder.Save(file);
    }
}
