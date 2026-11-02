using System.Collections.ObjectModel;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
using System.Windows.Markup;

namespace frontend;

[ContentProperty(nameof(Categories))]
public partial class SettingsBrowser : UserControl
{
    public static readonly DependencyProperty SaveCommandProperty = DependencyProperty.Register(
        nameof(SaveCommand), typeof(ICommand), typeof(SettingsBrowser));
    public static readonly DependencyProperty HasChangesProperty = DependencyProperty.Register(
        nameof(HasChanges), typeof(bool), typeof(SettingsBrowser),
        new PropertyMetadata(false, (d, _) => ((SettingsBrowser)d).UpdatePendingHint()));
    public static readonly DependencyProperty BackendAvailableProperty = DependencyProperty.Register(
        nameof(BackendAvailable), typeof(bool), typeof(SettingsBrowser), new PropertyMetadata(true));

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
    public SettingsCategory? SelectedCategory { get; private set; }
    private Button? _categoryButton;

    public SettingsBrowser()
    {
        InitializeComponent();
        CategoryList.ItemsSource = Categories;
        Categories.CollectionChanged += (_, _) => RebuildCategories();
        IsVisibleChanged += (_, _) => { if (IsVisible) ShowOverview(); };
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
        SaveFooter.Visibility = SelectedCategory.ShowSaveButton ? Visibility.Visible : Visibility.Collapsed;
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
        SaveFooter.Visibility = Visibility.Collapsed;
        PageTitle.Text = "Настройки";
        PageScroll.ScrollToTop();
        _categoryButton?.Focus();
    }

    private void UpdatePendingHint()
    {
        if (PendingHint != null)
            PendingHint.Visibility = HasChanges ? Visibility.Visible : Visibility.Collapsed;
    }
}
