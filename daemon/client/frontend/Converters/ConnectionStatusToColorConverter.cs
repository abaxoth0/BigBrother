using System.Globalization;
using System.Windows.Data;
using System.Windows.Media;
using WpfBrush = System.Windows.Media.SolidColorBrush;
using WpfColor = System.Windows.Media.Color;

namespace frontend.Converters;

public class ConnectionStatusToColorConverter : IValueConverter
{
    public object Convert(object value, Type targetType, object parameter, CultureInfo culture)
    {
        if (value is string status)
        {
            return status switch
            {
                "Подключено" => new WpfBrush(WpfColor.FromRgb(76, 175, 80)),
                "Отключено" => new WpfBrush(WpfColor.FromRgb(220, 20, 60)),
                "..." => new WpfBrush(WpfColor.FromRgb(255, 152, 0)),
                _ => new WpfBrush(Colors.Gray)
            };
        }
        return new WpfBrush(Colors.Gray);
    }

    public object ConvertBack(object value, Type targetType, object parameter, CultureInfo culture)
    {
        throw new NotImplementedException();
    }
}
