using System.Runtime.InteropServices;
using System.Security.Principal;
using System.Windows;

namespace frontend;

public partial class App : Application
{
    protected override void OnStartup(StartupEventArgs e)
    {
        if (!IsAdmin())
        {
            TryElevate();
            Shutdown(1);
            return;
        }
        base.OnStartup(e);
    }

    private static bool IsAdmin()
    {
        using var identity = WindowsIdentity.GetCurrent();
        var principal = new WindowsPrincipal(identity);
        return principal.IsInRole(WindowsBuiltInRole.Administrator);
    }

    private static void TryElevate()
    {
        try
        {
            var exePath = Environment.ProcessPath;
            if (exePath == null) return;
            var psi = new System.Diagnostics.ProcessStartInfo(exePath)
            {
                Verb = "runas",
                UseShellExecute = true
            };
            System.Diagnostics.Process.Start(psi);
        }
        catch { }
    }
}
