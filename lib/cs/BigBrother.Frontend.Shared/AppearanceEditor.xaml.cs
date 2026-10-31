using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
using frontend.Services;

namespace frontend;

public partial class AppearanceEditor : UserControl
{
    public AppearanceEditor() => InitializeComponent();

    private void CommitFontSize(TextBox input)
    {
        if (int.TryParse(input.Text, out int size))
        {
            if (input == TextFontSizeInput) AppearanceSettings.Current.TextFontSize = size;
            else AppearanceSettings.Current.InterfaceFontSize = size;
        }
        input.GetBindingExpression(TextBox.TextProperty)?.UpdateTarget();
    }

    private void ChangeFontSize(TextBox input, int delta)
    {
        CommitFontSize(input);
        if (input == TextFontSizeInput) AppearanceSettings.Current.TextFontSize += delta;
        else AppearanceSettings.Current.InterfaceFontSize += delta;
    }

    private void DecreaseText_Click(object sender, RoutedEventArgs e) => ChangeFontSize(TextFontSizeInput, -1);
    private void IncreaseText_Click(object sender, RoutedEventArgs e) => ChangeFontSize(TextFontSizeInput, 1);
    private void DecreaseInterface_Click(object sender, RoutedEventArgs e) => ChangeFontSize(InterfaceFontSizeInput, -1);
    private void IncreaseInterface_Click(object sender, RoutedEventArgs e) => ChangeFontSize(InterfaceFontSizeInput, 1);

    private void FontSizeInput_LostFocus(object sender, RoutedEventArgs e) => CommitFontSize((TextBox)sender);

    private void FontSizeInput_PreviewKeyDown(object sender, KeyEventArgs e)
    {
        var input = (TextBox)sender;
        switch (e.Key)
        {
            case Key.Enter:
                CommitFontSize(input);
                input.SelectAll();
                break;
            case Key.Escape:
                input.GetBindingExpression(TextBox.TextProperty)?.UpdateTarget();
                break;
            case Key.Up:
                ChangeFontSize(input, 1);
                break;
            case Key.Down:
                ChangeFontSize(input, -1);
                break;
            default:
                return;
        }
        e.Handled = true;
    }

    private void Reset_Click(object sender, RoutedEventArgs e)
    {
        AppearanceSettings.Current.TextFontSize = AppearancePreferences.DefaultFontSize;
        AppearanceSettings.Current.InterfaceFontSize = AppearancePreferences.DefaultFontSize;
        TextFontSizeInput.GetBindingExpression(TextBox.TextProperty)?.UpdateTarget();
        InterfaceFontSizeInput.GetBindingExpression(TextBox.TextProperty)?.UpdateTarget();
    }

    private void Save_Click(object sender, RoutedEventArgs e)
    {
        CommitFontSize(TextFontSizeInput);
        CommitFontSize(InterfaceFontSizeInput);
        AppearanceSettings.Current.Save();
    }
}
