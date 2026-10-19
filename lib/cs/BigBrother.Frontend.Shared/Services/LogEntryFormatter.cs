using frontend.Models;

namespace frontend.Services;

public static class LogEntryFormatter
{
    public static string Format(LogEntry entry) =>
        $"[{entry.Timestamp}] [{entry.Level}] {entry.Message}";

    public static string Format(ServerLogEntry entry) =>
        $"[{entry.Timestamp}] [{entry.Source}] [{entry.Level}] {entry.Message}";

    public static string Format(Lib.LogEntry entry, string source) =>
        $"[{entry.LocalTimestamp:yyyy-MM-dd HH:mm:ss}] [{source}] [{entry.Level}] {entry.Message}";
}
