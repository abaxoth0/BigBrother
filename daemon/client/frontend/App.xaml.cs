using System;
using System.Windows;
using System.Security.Principal;

namespace frontend;

public partial class App : System.Windows.Application
{
    private static bool IsAdmin()
    {
        using var identity = WindowsIdentity.GetCurrent();
        var principal = new WindowsPrincipal(identity);
        return principal.IsInRole(WindowsBuiltInRole.Administrator);
    }

    protected override void OnStartup(StartupEventArgs e)
    {
        if (!IsAdmin())
        {
            System.Windows.MessageBox.Show(
                "Эта программа требует права администратора.\nЗапустите от имени администратора.",
                "BigBrother - Ошибка доступа",
                MessageBoxButton.OK,
                MessageBoxImage.Error);
            Shutdown(1);
            return;
        }

        base.OnStartup(e);
    }
}