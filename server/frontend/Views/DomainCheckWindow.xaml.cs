using frontend.Models;
using frontend.ViewModels;

namespace frontend.Views;

public partial class DomainCheckWindow : ChromeWindow
{
    public DomainCheckWindow(IEnumerable<ComposedWhitelistEntry> entries)
    {
        InitializeComponent();
        DataContext = new DomainCheckViewModel(entries);
    }
}
