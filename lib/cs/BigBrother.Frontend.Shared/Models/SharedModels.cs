using System.ComponentModel;

namespace frontend.Models;

public class ServerLogEntry
{
    public string Timestamp { get; set; } = "";
    public string Level { get; set; } = "";
    public string Source { get; set; } = "";
    public string Message { get; set; } = "";
}

public class ConnectedClient : INotifyPropertyChanged
{
    public event PropertyChangedEventHandler? PropertyChanged;

    private string _address = "";
    private string _status = "";
    private string _whitelist = "";
    private DateTime _lastActivity;

    public string Name { get; set; } = "";
    public string Address
    {
        get => _address;
        set { if (_address != value) { _address = value; PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(nameof(Address))); } }
    }
    public string Status
    {
        get => _status;
        set { if (_status != value) { _status = value; PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(nameof(Status))); } }
    }
    public string Whitelist
    {
        get => _whitelist;
        set { if (_whitelist != value) { _whitelist = value; PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(nameof(Whitelist))); } }
    }
    public DateTime LastActivity
    {
        get => _lastActivity;
        set { if (_lastActivity != value) { _lastActivity = value; PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(nameof(LastActivity))); } }
    }
    public bool IsSelected { get; set; }
}

public class PendingRegistration : INotifyPropertyChanged
{
    public event PropertyChangedEventHandler? PropertyChanged;

    private string _address = "";
    private DateTime _createdAt;

    public string Name { get; set; } = "";
    public string Address
    {
        get => _address;
        set { if (_address != value) { _address = value; PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(nameof(Address))); } }
    }
    public DateTime CreatedAt
    {
        get => _createdAt;
        set { if (_createdAt != value) { _createdAt = value; PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(nameof(CreatedAt))); } }
    }
}

public class WhitelistInfo : INotifyPropertyChanged
{
    public event PropertyChangedEventHandler? PropertyChanged;

    private int _entryCount;
    private bool _isSelected;
    public string Name { get; set; } = "";
    public int EntryCount
    {
        get => _entryCount;
        set
        {
            if (_entryCount != value)
            {
                _entryCount = value;
                PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(nameof(EntryCount)));
            }
        }
    }
    public bool IsSelected
    {
        get => _isSelected;
        set
        {
            if (_isSelected != value)
            {
                _isSelected = value;
                PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(nameof(IsSelected)));
            }
        }
    }
}

public class ServerStatus
{
    public bool IsRunning { get; set; }
    public string UptimeText { get; set; } = "";
    public int ConnectedClients { get; set; }
    public int PendingCount { get; set; }
    public bool FiltrationEnabled { get; set; } = true;
}

public class ClientStatus
{
    public string ClientName { get; set; } = "";
    public string IpAddress { get; set; } = "";
    public string DaemonStatus { get; set; } = "NOT_RUNNING";
    public string ClientBackendStatus { get; set; } = "NOT_RUNNING";
    public string ServerRunning { get; set; } = "NOT_RUNNING";
    public string ServerSessionActive { get; set; } = "NOT_CONNECTED";
    public int ClientPid { get; set; }
    public bool IsConnected => ClientBackendStatus == "RUNNING";
    public bool IsServerRunning => ServerRunning == "RUNNING";
    public bool IsServerSessionActive => ServerSessionActive == "CONNECTED";
    public uint WhitelistRevision { get; set; }
    public bool FiltrationEnabled { get; set; } = true;
}

public class LogFileSource
{
    public string Label { get; set; } = "";
    public string FilePath { get; set; } = "";
    public Lib.LogSource Source { get; set; } = Lib.LogSource.Unknown;
}
