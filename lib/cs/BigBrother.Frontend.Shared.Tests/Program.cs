using System.Collections.Concurrent;
using System.Diagnostics;
using System.Text;
using frontend.Services;
using Lib;

static void Check(bool condition, string message)
{
    if (!condition) throw new Exception(message);
}

static async Task Until(Func<bool> condition, string message)
{
    var deadline = Stopwatch.StartNew();
    while (!condition())
    {
        if (deadline.Elapsed > TimeSpan.FromSeconds(5)) throw new Exception(message);
        await Task.Delay(10);
    }
}

static byte[] Record(string message)
{
    using var stream = new MemoryStream();
    using var writer = new BinaryWriter(stream);
    var text = Encoding.ASCII.GetBytes(message);
    writer.Write((ushort)LogType.Log);
    writer.Write((ushort)(10 + text.Length));
    writer.Write(DateTimeOffset.UtcNow.ToUnixTimeSeconds());
    writer.Write((byte)LogLevel.Info);
    writer.Write(text);
    writer.Write((byte)0);
    return stream.ToArray();
}

static void DisableNotifications(LogFileMonitor monitor)
{
    var field = typeof(LogFileMonitor).GetField("_watcher",
        System.Reflection.BindingFlags.Instance | System.Reflection.BindingFlags.NonPublic)!;
    ((FileSystemWatcher)field.GetValue(monitor)!).EnableRaisingEvents = false;
}

static LogFileMonitor MonitorOf(object reader)
{
    var field = reader.GetType().GetField("_monitor",
        System.Reflection.BindingFlags.Instance | System.Reflection.BindingFlags.NonPublic)!;
    return (LogFileMonitor)field.GetValue(reader)!;
}

WhitelistTests.Run();

var scheduled = new ConcurrentQueue<Action>();
var received = new List<int>();
LogUpdateQueue<int>? updates = null;
updates = new LogUpdateQueue<int>(500, scheduled.Enqueue, entries =>
{
    received.AddRange(entries);
    if (entries[^1] == 99999) updates!.Add(100000);
});
Parallel.For(0, 10000, updates.Add);
Check(scheduled.Count == 1, "Burst schedules only one UI callback");
while (scheduled.TryDequeue(out var callback)) callback();
Check(received.Count == 500, "Pending history is bounded");
received.Clear();
for (int i = 0; i < 100000; i++) updates.Add(i);
Check(scheduled.Count == 1, "Slow UI cannot accumulate callbacks");
scheduled.TryDequeue(out var drain);
drain!();
Check(received.SequenceEqual(Enumerable.Range(99500, 500)), "Slow UI receives newest history");
Check(scheduled.Count == 1, "Writes during rendering schedule a follow-up");
scheduled.TryDequeue(out drain);
drain!();
Check(received[^1] == 100000 && scheduled.IsEmpty, "No wakeups after draining");

var record = Record("split record");
for (int split = 1; split < record.Length; split++)
{
    var entries = LogParser.ParseAvailable(record[..split], out var consumed);
    Check(entries.Count == 0 && consumed == 0, "Incomplete binary record must be retained");
}

string directory = Path.Combine(Path.GetTempPath(), "bigbrother-log-tests-" + Guid.NewGuid());
Directory.CreateDirectory(directory);
try
{
    int reads = 0;
    using (var monitor = new LogFileMonitor(directory, "idle.binlog", () => Interlocked.Increment(ref reads)))
    {
        await Until(() => Volatile.Read(ref reads) == 1, "Initial notification");
        await Task.Delay(250);
        Check(reads == 1, "Idle monitor limits fallback reads");
        DisableNotifications(monitor);
        await Until(() => Volatile.Read(ref reads) >= 2, "Monitor retries without notifications");
    }

    string binary = Path.Combine(directory, "client.binlog");
    File.WriteAllBytes(binary, []);
    var lines = new ConcurrentQueue<string>();
    var reader = new LogReader();
    reader.OnNewLine += lines.Enqueue;
    reader.Start(binary, LogSource.Client, loadFullHistory: true);
    DisableNotifications(MonitorOf(reader));
    using (var output = new FileStream(binary, FileMode.Append, FileAccess.Write, FileShare.ReadWrite))
    {
        output.Write(Record("open binary writer"));
        output.Flush();
        await Until(() => lines.Any(line => line.EndsWith("open binary writer")),
            "Binary logs arrive without notifications before writer closes");
    }
    using (var output = new FileStream(binary, FileMode.Append, FileAccess.Write, FileShare.ReadWrite))
    {
        for (int i = 0; i < 10000; i++) output.Write(Record($"burst {i}"));
        output.Flush();
    }
    await Until(() => lines.Any(line => line.EndsWith("burst 9999")), "Binary reader drains beyond 64 KB promptly");
    Check(lines.Count(line => line.Contains("burst ")) == 10000, "Binary chunk boundaries preserve every record");
    using (var output = new FileStream(binary, FileMode.Append, FileAccess.Write, FileShare.ReadWrite))
    {
        output.Write(record.AsSpan(0, 7));
        output.Flush();
        await Task.Delay(150);
        output.Write(record.AsSpan(7));
        output.Flush();
    }
    await Until(() => lines.Any(line => line.EndsWith("split record")), "Binary record split across writes survives");
    File.WriteAllBytes(binary, Record("binary truncated"));
    await Until(() => lines.Any(line => line.EndsWith("binary truncated")), "Truncated binary file restarts at its first record");
    reader.Stop();
    int stoppedCount = lines.Count;
    File.AppendAllBytes(binary, Record("after stop"));
    await Task.Delay(150);
    Check(lines.Count == stoppedCount, "Stopped reader emits no further logs");

    string json = Path.Combine(directory, "server.log");
    File.WriteAllText(json, "");
    var serverLines = new ConcurrentQueue<string>();
    using var tailer = new ServerLogTailer();
    tailer.EntriesRead += entries =>
    {
        foreach (var entry in entries) serverLines.Enqueue(entry.Message);
    };
    tailer.SetLogPath(directory);
    DisableNotifications(MonitorOf(tailer));
    using (var output = new FileStream(json, FileMode.Append, FileAccess.Write, FileShare.ReadWrite))
    {
        output.Write(Encoding.UTF8.GetBytes("{\"msg\":\"open JSON writer\"}\n"));
        output.Flush();
        await Until(() => serverLines.Contains("open JSON writer"),
            "Server logs arrive without notifications before writer closes");
    }
    using (var output = new FileStream(json, FileMode.Append, FileAccess.Write, FileShare.ReadWrite))
    {
        output.Write(Encoding.UTF8.GetBytes("{\"msg\":\"split"));
        output.Flush();
        await Task.Delay(150);
        output.Write(Encoding.UTF8.GetBytes(" JSON\",\"level\":\"INFO\"}\n"));
        output.Flush();
    }
    await Until(() => serverLines.Contains("split JSON"), "JSON split across writes survives");
    using (var output = new FileStream(json, FileMode.Append, FileAccess.Write, FileShare.ReadWrite))
    {
        for (int i = 0; i < 100; i++)
        {
            output.Write(Encoding.UTF8.GetBytes($"{{\"msg\":\"continuous {i}\"}}\n"));
            output.Flush();
            await Task.Delay(10);
        }
    }
    await Until(() => serverLines.Contains("continuous 99"), "Latest JSON entry arrives promptly");
    File.WriteAllText(json, "{\"msg\":42}\n{\"msg\":\"truncated\"}\n");
    await Until(() => serverLines.Contains("truncated"), "Truncated JSON file is read from the start");
    string rotated = Path.Combine(directory, "new.log");
    File.WriteAllText(rotated, "{\"msg\":\"rotated\"}\n");
    await Until(() => serverLines.Contains("rotated"), "New server log is followed after rotation");
}
finally
{
    Directory.Delete(directory, true);
}
Console.WriteLine("PASS: bounded UI updates, notification fallback with open writers, binary bursts/split records, stop, continuous JSON writes, split lines and truncation");
