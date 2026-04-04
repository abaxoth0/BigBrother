namespace Lib;

public enum LogType : ushort
{
    Log = 1,
    Dlog = 2
}

public enum LogLevel : byte
{
    Info = 0,
    Error = 1,
    Debug = 2,
    Blocked = 3
}

public enum LogSource
{
    Unknown = 0,
    Frontend = 1,
    Firewall = 2,
    Client = 3
}

public class LogEntry
{
    public LogType Type { get; set; }
    public LogLevel Level { get; set; }
    public DateTime Timestamp { get; set; }
    public string Message { get; set; } = "";
    public LogSource Source { get; set; } = LogSource.Unknown;
}

public static class LogParser
{
    public const int HeaderSize = 4;

    public static LogEntry? ParseEntry(byte[] data, int offset)
    {
        if (offset + HeaderSize > data.Length)
            return null;

        // Read header: TYPE (2 bytes) + LEN (2 bytes) - little endian
        ushort type = (ushort)(data[offset] | (data[offset + 1] << 8));
        ushort len = (ushort)(data[offset + 2] | (data[offset + 3] << 8));

        if (len < 9 || offset + HeaderSize + len > data.Length)
            return null;

        // Parse payload
        int payloadOffset = offset + HeaderSize;

        // Timestamp: 8 bytes - little endian
        ulong timestamp = 0;
        for (int i = 0; i < 8; i++)
        {
            timestamp |= (ulong)data[payloadOffset + i] << (i * 8);
        }

        // Validate timestamp (must be reasonable Unix time: 1990-2100)
        if (timestamp < 631152000 || timestamp > 4102444800)
        {
            // Invalid timestamp, skip this entry
            return null;
        }

        // Level: 1 byte
        LogLevel level = (LogLevel)data[payloadOffset + 8];

        // Message: null-terminated string
        int msgOffset = payloadOffset + 9;
        int msgLen = len - 9;
        string message = "";
        for (int i = 0; i < msgLen; i++)
        {
            if (data[msgOffset + i] == 0)
                break;
            message += (char)data[msgOffset + i];
        }

        return new LogEntry
        {
            Type = (LogType)type,
            Level = level,
            Timestamp = DateTimeOffset.FromUnixTimeSeconds((long)timestamp).DateTime,
            Message = message
        };
    }

    public static List<LogEntry> ParseAll(byte[] data)
    {
        var entries = new List<LogEntry>();
        int offset = 0;

        while (offset < data.Length)
        {
            var entry = ParseEntry(data, offset);
            if (entry == null)
            {
                // Skip ahead by 1 byte and try again (in case of partial/corrupt data)
                offset++;
                continue;
            }

            entries.Add(entry);
            int len = (data[offset + 2] | (data[offset + 3] << 8));
            // System.Diagnostics.Debug.WriteLine($"[LogParser] Entry parsed: type={entry.Type}, level={entry.Level}, msg={entry.Message}, advancing offset by {HeaderSize + len}");
            offset += HeaderSize + len;
        }

        return entries;
    }
}
