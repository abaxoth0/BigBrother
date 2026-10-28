using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
using frontend.Services;

namespace frontend;

public partial class AppearanceEditor : UserControl
{
    public AppearanceEditor() => InitializeComponent();

    private void CommitFontSize()
    {
        if (int.TryParse(FontSizeInput.Text, out int size))
            AppearanceSettings.Current.BaseFontSize = size;
        FontSizeInput.GetBindingExpression(TextBox.TextProperty)?.UpdateTarget();
    }

    private void ChangeFontSize(int delta)
    {
        CommitFontSize();
        AppearanceSettings.Current.BaseFontSize += delta;
    }

    private void Decrease_Click(object sender, RoutedEventArgs e) => ChangeFontSize(-1);

    private void Increase_Click(object sender, RoutedEventArgs e) => ChangeFontSize(1);

    private void FontSizeInput_LostFocus(object sender, RoutedEventArgs e) => CommitFontSize();

    private void FontSizeInput_PreviewKeyDown(object sender, KeyEventArgs e)
    {
        switch (e.Key)
        {
            case Key.Enter:
                CommitFontSize();
                FontSizeInput.SelectAll();
                break;
            case Key.Escape:
                FontSizeInput.GetBindingExpression(TextBox.TextProperty)?.UpdateTarget();
                break;
            case Key.Up:
                ChangeFontSize(1);
                break;
            case Key.Down:
                ChangeFontSize(-1);
                break;
            default:
                return;
        }
        e.Handled = true;
    }

    private void Reset_Click(object sender, RoutedEventArgs e) =>
        AppearanceSettings.Current.BaseFontSize = AppearancePreferences.DefaultFontSize;

    private void Save_Click(object sender, RoutedEventArgs e)
    {
        CommitFontSize();
        AppearanceSettings.Current.Save();
    }
}
