using System.Windows;
using System.Windows.Input;

namespace frontend;

public class ServerInfo
{
    public string Name { get; set; } = "";
    public string Ip { get; set; } = "";
    public string DisplayText => $"{Name} ({Ip})";
}

public partial class ServerSelectWindow : Window
{
    public ServerInfo? SelectedServer { get; private set; }

    public ServerSelectWindow(List<ServerInfo> servers)
    {
        InitializeComponent();

        if (servers.Count == 0)
        {
            ServersListBox.Items.Add(new ServerInfo { Name = "Серверы не найдены", Ip = "" });
            ConnectButton.IsEnabled = false;
        }
        else
        {
            foreach (var s in servers)
                ServersListBox.Items.Add(s);
            ServersListBox.SelectedIndex = 0;
            ConnectButton.IsEnabled = true;
        }

        ServersListBox.MouseDoubleClick += (s, e) =>
        {
            if (ServersListBox.SelectedItem is ServerInfo si && !string.IsNullOrEmpty(si.Ip))
            {
                SelectedServer = si;
                DialogResult = true;
                Close();
            }
        };

        ServersListBox.SelectionChanged += (s, e) =>
        {
            ConnectButton.IsEnabled = ServersListBox.SelectedItem is ServerInfo si && !string.IsNullOrEmpty(si.Ip);
        };
    }

    private void ConnectButton_Click(object sender, RoutedEventArgs e)
    {
        if (ServersListBox.SelectedItem is ServerInfo si && !string.IsNullOrEmpty(si.Ip))
        {
            SelectedServer = si;
            DialogResult = true;
            Close();
        }
    }

    private void CancelButton_Click(object sender, RoutedEventArgs e)
    {
        DialogResult = false;
        Close();
    }
}
