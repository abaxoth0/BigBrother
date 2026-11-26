namespace frontend.ViewModels;

public sealed class ProcessBlacklistListViewModel : ViewModelBase
{
    private int _revision;
    private string _text = "";
    private string _message = "Чёрный список недоступен.";

    public string Text
    {
        get => _text;
        private set => SetProperty(ref _text, value);
    }

    public string Message
    {
        get => _message;
        private set => SetProperty(ref _message, value);
    }

    public async Task RefreshAsync(Func<Task<List<string>?>> load)
    {
        int revision = ++_revision;
        if (Text.Length == 0) Message = "Загрузка…";
        var rules = await load();
        if (revision != _revision) return;

        if (rules == null)
        {
            Clear();
            return;
        }

        Text = string.Join(Environment.NewLine, rules);
        Message = rules.Count == 0 ? "Чёрный список пуст." : "";
    }

    public void Clear()
    {
        ++_revision;
        Text = "";
        Message = "Чёрный список недоступен.";
    }
}
