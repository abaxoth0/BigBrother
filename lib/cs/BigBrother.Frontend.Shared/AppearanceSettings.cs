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

    public bool CanDecreaseFontSize => BaseFontSize > AppearancePreferences.MinimumFontSize;
    public bool CanIncreaseFontSize => BaseFontSize < AppearancePreferences.MaximumFontSize;

    public int BaseFontSize
    {
        get => _preferences.BaseFontSize;
        set
        {
            value = Math.Clamp(value, AppearancePreferences.MinimumFontSize, AppearancePreferences.MaximumFontSize);
            if (_preferences.BaseFontSize == value) return;
            _preferences.BaseFontSize = value;
            ApplyResources();
            OnPropertyChanged();
            OnPropertyChanged(nameof(CanDecreaseFontSize));
            OnPropertyChanged(nameof(CanIncreaseFontSize));
            Save();
        }
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
        ApplyResources();
        OnPropertyChanged(nameof(BaseFontSize));
    }

    public void Save()
    {
        if (_path == null) return;
        try
        {
            _preferences.Save(_path);
            Status = "Размер шрифта сохранён.";
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException)
        {
            Status = "Не удалось сохранить размер шрифта: " + ex.Message;
        }
    }

    private void ApplyResources()
    {
        var resources = Application.Current.Resources;
        for (int size = 10; size <= 28; size++)
            resources[$"FontSize{size}"] = size * BaseFontSize / (double)AppearancePreferences.DefaultFontSize;
        foreach (int size in new[] { 24, 26, 28, 40, 42, 46, 90, 120, 190, 220, 270 })
            resources[$"Scaled{size}"] = size * BaseFontSize / (double)AppearancePreferences.DefaultFontSize;
    }
}
