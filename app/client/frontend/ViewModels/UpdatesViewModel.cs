using System.Windows.Input;
using frontend.Services;

namespace frontend.ViewModels;

public sealed class UpdatesViewModel : ViewModelBase
{
    private readonly Func<Task<ClientUpdate?>> _check;
    private readonly Func<ClientUpdate, Task> _install;
    private ClientUpdate? _available;
    private bool _busy;
    private string _status = "Нажмите «Проверить обновления», чтобы узнать о новых версиях.";

    public UpdatesViewModel(string installedVersion, Func<Task<ClientUpdate?>> check, Func<ClientUpdate, Task> install)
    {
        InstalledVersion = installedVersion;
        _check = check;
        _install = install;
        CheckCommand = new RelayCommand(async _ => await CheckAsync(), _ => !Busy);
        InstallCommand = new RelayCommand(async _ => await InstallAsync(), _ => CanInstall);
    }

    public string InstalledVersion { get; }
    public string Status { get => _status; private set => SetProperty(ref _status, value); }
    public bool Busy { get => _busy; private set { SetProperty(ref _busy, value); OnPropertyChanged(nameof(CanInstall)); } }
    public bool CanInstall => !Busy && _available != null;
    public string AvailableVersion => _available?.Version ?? "—";
    public string NotesTitle => _available?.ReleaseNotes is { Version: var version } && !version.Equals("Unreleased", StringComparison.OrdinalIgnoreCase)
        ? $"Изменения в версии {version}" : "Что изменилось";
    public ReleaseNoteSection[] Sections => _available?.ReleaseNotes?.Sections ?? [];
    public string NotesHint => _available == null ? "" : Sections.Length == 0 ? "Для этой версии описание изменений не предоставлено." : "";
    public ICommand CheckCommand { get; }
    public ICommand InstallCommand { get; }

    public async Task CheckAsync()
    {
        if (Busy) return;
        Busy = true;
        _available = null;
        RefreshAvailable();
        Status = "Проверка обновлений…";
        try
        {
            var update = await _check();
            if (update == null) { Status = "Сервер не предлагает обновлений."; return; }
            update.Validate();
            if (!update.IsNewerThan(InstalledVersion)) { Status = "Установлена актуальная версия."; return; }
            _available = update;
            Status = "Доступно обновление. Просмотрите изменения и нажмите «Установить обновление».";
        }
        catch (Exception ex) { Status = "Не удалось проверить обновления: " + ex.Message; }
        finally { Busy = false; RefreshAvailable(); }
    }

    public async Task InstallAsync()
    {
        if (!CanInstall) return;
        var update = _available!;
        Busy = true;
        Status = "Подготовка обновления…";
        try { await _install(update); }
        catch (Exception ex)
        {
            _available = null;
            Status = "Не удалось начать обновление: " + ex.Message + " Проверьте обновления ещё раз.";
        }
        finally { Busy = false; RefreshAvailable(); }
    }

    private void RefreshAvailable()
    {
        OnPropertyChanged(nameof(CanInstall));
        OnPropertyChanged(nameof(AvailableVersion));
        OnPropertyChanged(nameof(NotesTitle));
        OnPropertyChanged(nameof(Sections));
        OnPropertyChanged(nameof(NotesHint));
        CommandManager.InvalidateRequerySuggested();
    }
}
