using System.Globalization;
using System.Windows.Data;
using System.Windows.Media;

namespace frontend.Converters;

public class ConnectionStatusToColorConverter : IValueConverter
{
    public object Convert(object value, Type targetType, object parameter, CultureInfo culture)
    {
        if (value is string status)
        {
            return status switch
            {
                "Подключено" => new SolidColorBrush(Color.FromRgb(76, 175, 80)),
                "Отключено" => new SolidColorBrush(Color.FromRgb(220, 20, 60)),
                "..." => new SolidColorBrush(Color.FromRgb(255, 152, 0)),
                _ => new SolidColorBrush(Colors.Gray)
            };
        }
        return new SolidColorBrush(Colors.Gray);
    }

    public object ConvertBack(object value, Type targetType, object parameter, CultureInfo culture)
    {
        throw new NotImplementedException();
    }
}
