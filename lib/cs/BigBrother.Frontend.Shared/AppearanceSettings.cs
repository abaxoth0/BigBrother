using System.IO;
using System.Windows;
using frontend.Services;
using frontend.ViewModels;

namespace frontend;

public sealed class AppearanceSettings : ViewModelBase
{
    public static AppearanceSettings Current { get; } = new();

    private AppearancePreferences _preferences = new();
    private string? _path;
    private string _status = "";
    private int _savedTextFontSize = AppearancePreferences.DefaultFontSize;
    private int _savedInterfaceFontSize = AppearancePreferences.DefaultFontSize;

    public bool HasChanges => TextFontSize != _savedTextFontSize || InterfaceFontSize != _savedInterfaceFontSize;

    public bool CanDecreaseTextFontSize => TextFontSize > AppearancePreferences.MinimumFontSize;
    public bool CanIncreaseTextFontSize => TextFontSize < AppearancePreferences.MaximumFontSize;
    public bool CanDecreaseInterfaceFontSize => InterfaceFontSize > AppearancePreferences.MinimumFontSize;
    public bool CanIncreaseInterfaceFontSize => InterfaceFontSize < AppearancePreferences.MaximumFontSize;

    public int TextFontSize
    {
        get => _preferences.TextFontSize;
        set
        {
            value = Math.Clamp(value, AppearancePreferences.MinimumFontSize, AppearancePreferences.MaximumFontSize);
            if (_preferences.TextFontSize == value) return;
            _preferences.TextFontSize = value;
            SizeChanged(nameof(TextFontSize));
        }
    }

    public int InterfaceFontSize
    {
        get => _preferences.InterfaceFontSize;
        set
        {
            value = Math.Clamp(value, AppearancePreferences.MinimumFontSize, AppearancePreferences.MaximumFontSize);
            if (_preferences.InterfaceFontSize == value) return;
            _preferences.InterfaceFontSize = value;
            SizeChanged(nameof(InterfaceFontSize));
        }
    }

    private void SizeChanged(string property)
    {
        ApplyResources();
        OnPropertyChanged(property);
        NotifyLimitsChanged();
        Status = "";
        OnPropertyChanged(nameof(HasChanges));
    }

    private void NotifyLimitsChanged()
    {
        OnPropertyChanged(nameof(CanDecreaseTextFontSize));
        OnPropertyChanged(nameof(CanIncreaseTextFontSize));
        OnPropertyChanged(nameof(CanDecreaseInterfaceFontSize));
        OnPropertyChanged(nameof(CanIncreaseInterfaceFontSize));
    }

    public string Status
    {
        get => _status;
        private set => SetProperty(ref _status, value);
    }

    public void Initialize(string application)
    {
        _path = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
            "BigBrother", application, "appearance.json");
        _preferences = AppearancePreferences.Load(_path);
        _savedTextFontSize = TextFontSize;
        _savedInterfaceFontSize = InterfaceFontSize;
        Status = "";
        ApplyResources();
        OnPropertyChanged(nameof(TextFontSize));
        OnPropertyChanged(nameof(InterfaceFontSize));
        NotifyLimitsChanged();
        OnPropertyChanged(nameof(HasChanges));
    }

    public void Save()
    {
        if (_path == null) return;
        try
        {
            _preferences.Save(_path);
            _savedTextFontSize = TextFontSize;
            _savedInterfaceFontSize = InterfaceFontSize;
            OnPropertyChanged(nameof(HasChanges));
            Status = "Размеры шрифтов сохранены.";
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException)
        {
            Status = "Не удалось сохранить размеры шрифтов: " + ex.Message;
        }
    }

    private void ApplyResources()
    {
        var resources = Application.Current.Resources;
        double textScale = TextFontSize / (double)AppearancePreferences.DefaultFontSize;
        double interfaceScale = InterfaceFontSize / (double)AppearancePreferences.DefaultFontSize;
        for (int size = 10; size <= 28; size++)
        {
            resources[$"FontSize{size}"] = size * InterfaceFontSize / (double)AppearancePreferences.DefaultFontSize;
            resources[$"TextFontSize{size}"] = size * TextFontSize / (double)AppearancePreferences.DefaultFontSize;
        }
        foreach (int size in new[] { 24, 26, 28, 40, 42, 46, 90, 120, 190, 220, 270 })
            resources[$"Scaled{size}"] = size * InterfaceFontSize / (double)AppearancePreferences.DefaultFontSize;
        resources["TableRowMinHeight"] = 32 * textScale;
        resources["TableHeaderMinHeight"] = 32 * interfaceScale;
        resources["LogRowMinHeight"] = 22 * textScale;
        resources["LogRowPadding"] = new Thickness(4, 2 * textScale, 4, 2 * textScale);
        resources["RowPadding"] = new Thickness(4, 4 * textScale, 4, 4 * textScale);
        resources["TableHeaderPadding"] = new Thickness(6, 6 * interfaceScale, 6, 6 * interfaceScale);
    }
}
