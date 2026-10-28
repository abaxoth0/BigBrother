using System.Windows;
using System.Windows.Controls;
using frontend.Services;

namespace frontend;

public partial class AppearanceEditor : UserControl
{
    public AppearanceEditor() => InitializeComponent();

    private void Reset_Click(object sender, RoutedEventArgs e) =>
        AppearanceSettings.Current.BaseFontSize = AppearancePreferences.DefaultFontSize;

    private void Save_Click(object sender, RoutedEventArgs e) => AppearanceSettings.Current.Save();
}
