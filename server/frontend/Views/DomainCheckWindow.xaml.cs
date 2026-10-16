using frontend.ViewModels;

namespace frontend.Views;

public partial class DomainCheckWindow : ChromeWindow
{
    public DomainCheckWindow(IEnumerable<string> entries)
    {
        InitializeComponent();
        DataContext = new DomainCheckViewModel(entries);
    }
}
