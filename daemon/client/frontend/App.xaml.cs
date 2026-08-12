using System.Windows;

namespace frontend;

public partial class App : System.Windows.Application
{
    protected override void OnStartup(StartupEventArgs e)
    {
        if (!ElevationHelper.IsAdmin())
        {
            ElevationHelper.TryElevate();
            Shutdown(1);
            return;
        }

        base.OnStartup(e);
    }
}
