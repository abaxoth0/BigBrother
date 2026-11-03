using System.Windows;

namespace frontend;

public partial class App : Application
{
    protected override void OnStartup(StartupEventArgs e)
    {
        if (!ElevationHelper.IsAdmin())
        {
            ElevationHelper.TryElevate();
            Shutdown(1);
            return;
        }
        AppearanceSettings.Current.Initialize("Server");
        base.OnStartup(e);
    }
}
