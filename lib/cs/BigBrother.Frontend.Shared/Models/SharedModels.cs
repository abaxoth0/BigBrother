using System.ComponentModel;
using System.Diagnostics;

namespace frontend.Models;

public class ServerLogEntry
{
    public string Timestamp { get; set; } = "";
    public string Level { get; set; } = "";
    public string Source { get; set; } = "";
    public string Message { get; set; } = "";
}

public interface ISelectableRow : INotifyPropertyChanged
{
    bool IsSelected { get; set; }
}

public class ConnectedClient : ISelectableRow
{
    public event PropertyChangedEventHandler? PropertyChanged;

    private string _address = "";
    private string _status = "Подключён";
    private ClientPolicyStatus? _policy;
    private bool? _whitelistCurrent;
    private DateTime? _lastActivity;
    private bool _isSelected;

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
    public ClientPolicyStatus? Policy
    {
        get => _policy;
        set
        {
            _policy = value;
            foreach (var name in new[] { nameof(Policy), nameof(Whitelist), nameof(FiltrationText), nameof(Ipv6BlockText) })
                PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(name));
        }
    }
    public bool? WhitelistCurrent
    {
        get => _whitelistCurrent;
        set
        {
            if (_whitelistCurrent == value) return;
            _whitelistCurrent = value;
            PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(nameof(WhitelistCurrent)));
            PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(nameof(Whitelist)));
        }
    }
    public string Whitelist => WhitelistCurrent switch
    {
        true => "Актуален",
        false => "Устарел",
        null => Policy == null ? "Нет данных" : "Недоступен"
    };
    public string FiltrationText => Policy == null ? "Нет данных" : Policy.FiltrationEnabled ? "Вкл" : "Выкл";
    public string Ipv6BlockText => Policy == null ? "Нет данных" : Policy.BlockIpv6 ? "Вкл" : "Выкл";
    public DateTime? LastActivity
    {
        get => _lastActivity;
        set { if (_lastActivity != value) { _lastActivity = value; PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(nameof(LastActivity))); } }
    }
    public bool IsSelected
    {
        get => _isSelected;
        set { if (_isSelected != value) { _isSelected = value; PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(nameof(IsSelected))); } }
    }
}

public class ClientPolicyStatus
{
    public string WhitelistRevision { get; set; } = "";
    public bool FiltrationEnabled { get; set; }
    public bool BlockIpv6 { get; set; }
    public bool CanSetIpv6Block { get; set; }
}

public class PendingRegistration : ISelectableRow
{
    public event PropertyChangedEventHandler? PropertyChanged;

    private string _address = "";
    private DateTime _createdAt;
    private bool _isSelected;

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
    public bool IsSelected
    {
        get => _isSelected;
        set { if (_isSelected != value) { _isSelected = value; PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(nameof(IsSelected))); } }
    }
}

public class WhitelistInfo : ISelectableRow
{
    public event PropertyChangedEventHandler? PropertyChanged;

    private bool _isActive;
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
    public bool IsActive
    {
        get => _isActive;
        set
        {
            if (_isActive != value)
            {
                _isActive = value;
                PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(nameof(IsActive)));
            }
        }
    }
    private long _timerTimestamp;
    private bool _timerRunning;
    public WhitelistTimerInfo? Temporary { get; set; }
    public bool IsTemporary => Temporary != null;
    public bool TimerIsRunning => _timerRunning;
    public double RemainingSeconds => Math.Max(0, (Temporary?.RemainingSeconds ?? 0) -
        (_timerRunning ? Stopwatch.GetElapsedTime(_timerTimestamp).TotalSeconds : 0));
    public string DurationText => FormatTimer(Temporary?.DurationSeconds ?? 0);
    public string RemainingText => FormatTimer((long)Math.Ceiling(RemainingSeconds));
    public string AutoDeleteText => Temporary?.AutoDelete == true ? "Да" : "Нет";

    private static string FormatTimer(long seconds) => $"{seconds / 3600:00}:{seconds / 60 % 60:00}:{seconds % 60:00}";

    public void UpdateTimer(WhitelistTimerInfo? timer, bool running)
    {
        Temporary = timer;
        _timerRunning = timer != null && running;
        _timerTimestamp = Stopwatch.GetTimestamp();
        PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(nameof(DurationText)));
        PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(nameof(AutoDeleteText)));
        TickTimer();
    }

    public void TickTimer() => PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(nameof(RemainingText)));

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

public class ComposedWhitelistEntry
{
    public string Value { get; set; } = "";
    public List<string> Sources { get; set; } = new();
}

public class WhitelistConflict
{
    public string Pattern { get; set; } = "";
    public List<string> AllowedBy { get; set; } = new();
    public List<string> ExcludedBy { get; set; } = new();
    public string AllowedRule => Pattern;
    public string ExcludedRule => "!" + Pattern;
    public string AllowedSources => string.Join(", ", AllowedBy);
    public string ExcludedSources => string.Join(", ", ExcludedBy);
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
    public bool FiltrationEnabled { get; set; } = false;
}

public class LogFileSource
{
    public string Label { get; set; } = "";
    public string FilePath { get; set; } = "";
    public Lib.LogSource Source { get; set; } = Lib.LogSource.Unknown;
}

public class WhitelistTimerInfo
{
    public long DurationSeconds { get; set; }
    public double RemainingSeconds { get; set; }
    public bool AutoDelete { get; set; }
}
