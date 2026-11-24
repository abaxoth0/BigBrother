// Only WPF's command requery hook is substituted for these cross-platform tests.
namespace System.Windows.Input;

public static class CommandManager
{
    public static event EventHandler? RequerySuggested;
    public static void InvalidateRequerySuggested() => RequerySuggested?.Invoke(null, EventArgs.Empty);
}
