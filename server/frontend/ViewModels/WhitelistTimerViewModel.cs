using System.Globalization;
using frontend.Models;

namespace frontend.ViewModels;

public class WhitelistTimerViewModel : ViewModelBase
{
    public const long MaxSeconds = long.MaxValue / 1_000_000_000;
    private string _duration;
    private string _remaining;
    private readonly string _initialRemaining;
    private bool _autoDelete;

    public WhitelistTimerViewModel(WhitelistInfo whitelist)
    {
        _duration = Format(whitelist.Temporary!.DurationSeconds);
        _remaining = _initialRemaining = Format((long)Math.Ceiling(whitelist.RemainingSeconds));
        _autoDelete = whitelist.Temporary.AutoDelete;
    }

    public string Duration
    {
        get => _duration;
        set { if (SetProperty(ref _duration, value)) NotifyValidation(); }
    }

    public string Remaining
    {
        get => _remaining;
        set { if (SetProperty(ref _remaining, value)) NotifyValidation(); }
    }

    public bool AutoDelete { get => _autoDelete; set => SetProperty(ref _autoDelete, value); }
    public bool RemainingChanged => Remaining != _initialRemaining;
    public long DurationSeconds => TryParse(Duration, out var value) ? value : 0;
    public long? RemainingSeconds => RemainingChanged && TryParse(Remaining, out var value) ? value : null;
    public bool IsValid => TryParse(Duration, out var duration) && duration > 0 && TryParse(Remaining, out _);
    public string ValidationMessage => IsValid ? "" : "Введите время в формате чч:мм:сс. Начальное время должно быть больше нуля.";

    private void NotifyValidation()
    {
        OnPropertyChanged(nameof(IsValid));
        OnPropertyChanged(nameof(ValidationMessage));
    }

    public static string Format(long seconds) => $"{seconds / 3600:00}:{seconds / 60 % 60:00}:{seconds % 60:00}";

    public static bool TryParse(string text, out long seconds)
    {
        seconds = 0;
        var parts = text.Trim().Split(':');
        if (parts.Length != 3 || !long.TryParse(parts[0], NumberStyles.None, CultureInfo.InvariantCulture, out var hours) ||
            !int.TryParse(parts[1], NumberStyles.None, CultureInfo.InvariantCulture, out var minutes) || minutes >= 60 ||
            !int.TryParse(parts[2], NumberStyles.None, CultureInfo.InvariantCulture, out var secs) || secs >= 60 ||
            hours > MaxSeconds / 3600) return false;
        seconds = hours * 3600 + minutes * 60 + secs;
        return seconds <= MaxSeconds;
    }
}
