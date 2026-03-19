using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
using frontend.ViewModels;

namespace frontend
{
    public partial class MainWindow : Window
    {
        public MainWindow()
        {
            InitializeComponent();

            if (DataContext is MainViewModel vm)
            {
                vm.ScrollToBottomRequested += () =>
                {
                    if (LogsListBox.Items.Count > 0)
                    {
                        LogsListBox.ScrollIntoView(LogsListBox.Items[^1]);
                    }
                };
            }
        }
    }
}
