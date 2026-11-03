using System.Collections.ObjectModel;
using System.ComponentModel;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
using System.Windows.Markup;
using frontend.ViewModels;

namespace frontend;

[ContentProperty(nameof(Categories))]
public partial class SettingsBrowser : UserControl
{
    public static readonly DependencyProperty SaveCommandProperty = DependencyProperty.Register(
        nameof(SaveCommand), typeof(ICommand), typeof(SettingsBrowser),
        new PropertyMetadata(null, (d, _) => ((SettingsBrowser)d).UpdateSaveState()));
    public static readonly DependencyProperty HasChangesProperty = DependencyProperty.Register(
        nameof(HasChanges), typeof(bool), typeof(SettingsBrowser),
        new PropertyMetadata(false, (d, _) => ((SettingsBrowser)d).UpdateSaveState()));
    public static readonly DependencyProperty BackendAvailableProperty = DependencyProperty.Register(
        nameof(BackendAvailable), typeof(bool), typeof(SettingsBrowser),
        new PropertyMetadata(true, (d, _) => ((SettingsBrowser)d).UpdateSaveState()));

    public ICommand? SaveCommand
    {
        get => (ICommand?)GetValue(SaveCommandProperty);
        set => SetValue(SaveCommandProperty, value);
    }

    public bool HasChanges
    {
        get => (bool)GetValue(HasChangesProperty);
        set => SetValue(HasChangesProperty, value);
    }

    public bool BackendAvailable
    {
        get => (bool)GetValue(BackendAvailableProperty);
        set => SetValue(BackendAvailableProperty, value);
    }

    public ObservableCollection<SettingsCategory> Categories { get; } = new();
    public ICommand SaveAllCommand { get; }
    public SettingsCategory? SelectedCategory { get; private set; }
    private Button? _categoryButton;
    private IEnumerable<AppearanceEditor> AppearanceEditors => Categories.Select(category => category.Content).OfType<AppearanceEditor>();
    private bool HasPendingChanges => HasChanges || AppearanceSettings.Current.HasChanges ||
                                      AppearanceEditors.Any(editor => editor.HasUncommittedChanges);

    public SettingsBrowser()
    {
        SaveAllCommand = new RelayCommand(_ => SaveAllSettings(), _ => CanSaveSettings());
        InitializeComponent();
        CategoryList.ItemsSource = Categories;
        Categories.CollectionChanged += (_, _) => RebuildCategories();
        IsVisibleChanged += (_, _) => { if (IsVisible) ShowOverview(); };
        Loaded += (_, _) =>
        {
            AppearanceSettings.Current.PropertyChanged += AppearanceChanged;
            UpdateSaveState();
        };
        Unloaded += (_, _) => AppearanceSettings.Current.PropertyChanged -= AppearanceChanged;
        AddHandler(System.Windows.Controls.Primitives.TextBoxBase.TextChangedEvent,
            new TextChangedEventHandler((_, _) => UpdateSaveState()));
        KeyDown += (_, e) =>
        {
            if (e.Key == Key.Escape && SelectedCategory != null)
            {
                ShowOverview();
                e.Handled = true;
            }
        };
    }

    private void RebuildCategories()
    {
        foreach (Grid panel in CategoryHost.Children) panel.Children.Clear();
        CategoryHost.Children.Clear();
        foreach (var category in Categories)
        {
            var panel = new Grid { Visibility = Visibility.Collapsed };
            panel.Children.Add(category.Content);
            CategoryHost.Children.Add(panel);
        }
        ShowOverview();
    }

    private void Category_Click(object sender, RoutedEventArgs e)
    {
        _categoryButton = (Button)sender;
        SelectedCategory = (SettingsCategory)_categoryButton.Tag;
        for (int index = 0; index < Categories.Count; index++)
            ((UIElement)CategoryHost.Children[index]).Visibility = Categories[index] == SelectedCategory
                ? Visibility.Visible : Visibility.Collapsed;
        CategoryList.Visibility = Visibility.Collapsed;
        CategoryHost.Visibility = Visibility.Visible;
        BackButton.Visibility = Visibility.Visible;
        OverviewHint.Visibility = Visibility.Collapsed;
        PageTitle.Text = SelectedCategory.Title;
        PageScroll.ScrollToTop();
        BackButton.Focus();
    }

    private void Back_Click(object sender, RoutedEventArgs e) => ShowOverview();

    private void ShowOverview()
    {
        SelectedCategory = null;
        CategoryList.Visibility = Visibility.Visible;
        CategoryHost.Visibility = Visibility.Collapsed;
        BackButton.Visibility = Visibility.Collapsed;
        OverviewHint.Visibility = Visibility.Visible;
        PageTitle.Text = "Настройки";
        PageScroll.ScrollToTop();
        _categoryButton?.Focus();
    }

    private bool CanSaveSettings() => HasPendingChanges &&
        (!HasChanges || BackendAvailable && SaveCommand?.CanExecute(null) == true);

    private void SaveAllSettings()
    {
        if (!CanSaveSettings()) return;
        foreach (var editor in AppearanceEditors) editor.CommitPendingChanges();
        if (HasChanges) SaveCommand?.Execute(null);
        if (AppearanceSettings.Current.HasChanges) AppearanceSettings.Current.Save();
        UpdateSaveState();
    }

    private void AppearanceChanged(object? sender, PropertyChangedEventArgs e)
    {
        if (e.PropertyName == nameof(AppearanceSettings.HasChanges)) UpdateSaveState();
    }

    private void UpdateSaveState()
    {
        if (PendingHint != null)
            PendingHint.Visibility = HasPendingChanges ? Visibility.Visible : Visibility.Collapsed;
        CommandManager.InvalidateRequerySuggested();
    }
}
