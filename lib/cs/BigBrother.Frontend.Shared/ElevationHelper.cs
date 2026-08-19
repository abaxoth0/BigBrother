using System.Security.Principal;

namespace frontend;

public static class ElevationHelper
{
    public static bool IsAdmin()
    {
        using var identity = WindowsIdentity.GetCurrent();
        var principal = new WindowsPrincipal(identity);
        return principal.IsInRole(WindowsBuiltInRole.Administrator);
    }

    public static void TryElevate()
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
