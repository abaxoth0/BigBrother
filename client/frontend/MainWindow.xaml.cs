using System;
using System.Windows;
using frontend.Models;
using frontend.ViewModels;

namespace frontend
{
    public partial class MainWindow : ChromeWindow
    {
        private Action? _scrollToBottomHandler;

        public MainWindow()
        {
            InitializeComponent();

            if (DataContext is MainViewModel vm)
            {
                _scrollToBottomHandler = () =>
                {
                    if (LogsListBox.Items.Count > 0)
                    {
                        LogsListBox.ScrollIntoView(LogsListBox.Items[^1]);
                    }
                };
                vm.ScrollToBottomRequested += _scrollToBottomHandler;
                vm.LogViewRequested += OpenLogView;
            }

            _ = InitializeAsync();
        }

        private async System.Threading.Tasks.Task InitializeAsync()
        {
            // Initial status fetch populates the UI and starts log readers.
            if (DataContext is MainViewModel vm)
            {
                await vm.RefreshStateAsync();
            }
        }

        private void OpenLogView(List<LogFileSource> sources)
        {
            var viewWindow = new LogViewWindow(sources) { Owner = this };
            viewWindow.Show();
        }

        protected override void OnClosed(EventArgs e)
        {
            if (DataContext is MainViewModel vm)
            {
                if (_scrollToBottomHandler != null)
                    vm.ScrollToBottomRequested -= _scrollToBottomHandler;
                vm.LogViewRequested -= OpenLogView;
            }
            if (DataContext is IDisposable disposable)
                disposable.Dispose();
            base.OnClosed(e);
        }
    }
}