namespace frontend.Services;

public class LogReader
{
    public void Start(string filePath) { }
    public void Stop() { }
#pragma warning disable CS0067
    public event Action<string>? OnNewLine;
#pragma warning restore CS0067
}
