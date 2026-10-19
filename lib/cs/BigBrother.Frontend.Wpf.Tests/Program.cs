using System.Diagnostics;
using System.IO;
using System.Text;
using System.Windows.Threading;
using frontend.Services;
using Lib;

internal static partial class Program
{
    private static void Check(bool condition, string message)
    {
        if (!condition) throw new Exception(message);
    }

    private static void PumpUntil(Func<bool> condition, string message, bool busyRendering = true)
    {
        var dispatcher = Dispatcher.CurrentDispatcher;
        var frame = new DispatcherFrame();
        var elapsed = Stopwatch.StartNew();
        bool finished = false;
        Action busy = null!;
        busy = () =>
        {
            if (!frame.Continue) return;
            Thread.SpinWait(1000);
            dispatcher.BeginInvoke(DispatcherPriority.Render, busy);
        };
        if (busyRendering) dispatcher.BeginInvoke(DispatcherPriority.Render, busy);
        var check = new DispatcherTimer(TimeSpan.FromMilliseconds(20), DispatcherPriority.Normal,
            (_, _) =>
            {
                finished = condition();
                if (finished || elapsed.Elapsed > TimeSpan.FromSeconds(5)) frame.Continue = false;
            }, dispatcher);
        Dispatcher.PushFrame(frame);
        check.Stop();
        Check(finished, message);
    }

    private static byte[] Record(string message)
    {
        using var data = new MemoryStream();
        using var writer = new BinaryWriter(data);
        var text = Encoding.ASCII.GetBytes(message);
        writer.Write((ushort)LogType.Log);
        writer.Write((ushort)(10 + text.Length));
        writer.Write(DateTimeOffset.UtcNow.ToUnixTimeSeconds());
        writer.Write((byte)LogLevel.Info);
        writer.Write(text);
        writer.Write((byte)0);
        return data.ToArray();
    }

    [STAThread]
    private static void Main()
    {
        var dispatcher = Dispatcher.CurrentDispatcher;
        SynchronizationContext.SetSynchronizationContext(new DispatcherSynchronizationContext(dispatcher));
        var batches = new List<List<int>>();
        using var updates = new DispatcherLogQueue<int>(8, dispatcher, batches.Add);
        for (int i = 0; i < 1000; i++) updates.Add(i);
        PumpUntil(() => batches.Count > 0, "Log display starved behind continuous rendering");
        Check(batches.Count == 1 && batches[0].SequenceEqual(Enumerable.Range(992, 8)),
            "A burst must render only the newest bounded batch");
        updates.Add(1000);
        Check(batches.Count == 1, "Producer must not update the display inline");
        PumpUntil(() => batches.Count == 2, "Subsequent updates stalled");
        updates.Dispose();
        updates.Add(1001);
        bool heartbeatSeen = false;
        using var stoppedCheck = new DispatcherLogQueue<int>(1, dispatcher, _ => heartbeatSeen = true);
        stoppedCheck.Add(1);
        PumpUntil(() => heartbeatSeen, "Dispatcher did not advance after disposal");
        Check(batches.Count == 2, "Disposed display queue must not consume entries");

        string directory = Path.Combine(Path.GetTempPath(), "bigbrother-wpf-log-tests-" + Guid.NewGuid());
        Directory.CreateDirectory(directory);
        try
        {
            string binary = Path.Combine(directory, "client.binlog");
            string json = Path.Combine(directory, "server.log");
            using var binaryWriter = new FileStream(binary, FileMode.Create, FileAccess.Write, FileShare.ReadWrite);
            using var jsonWriter = new FileStream(json, FileMode.Create, FileAccess.Write, FileShare.ReadWrite);
            int binaryDisplayed = 0, jsonDisplayed = 0;
            using var binaryUpdates = new DispatcherLogQueue<string>(500, dispatcher,
                entries => binaryDisplayed += entries.Count(line => line.Contains("live binary")));
            using var jsonUpdates = new DispatcherLogQueue<frontend.Models.ServerLogEntry>(2000, dispatcher,
                entries => jsonDisplayed += entries.Count);
            var reader = new LogReader();
            using var tailer = new ServerLogTailer();
            reader.OnNewLine += binaryUpdates.Add;
            tailer.EntriesRead += entries => entries.ForEach(jsonUpdates.Add);
            reader.Start(binary, LogSource.Client, loadFullHistory: true);
            tailer.SetLogPath(directory);
            var writerTimer = new DispatcherTimer(TimeSpan.FromMilliseconds(50), DispatcherPriority.Normal,
                (_, _) =>
                {
                    binaryWriter.Write(Record("live binary"));
                    binaryWriter.Flush();
                    jsonWriter.Write(Encoding.UTF8.GetBytes("{\"msg\":\"live JSON\"}\n"));
                    jsonWriter.Flush();
                }, dispatcher);
            try
            {
                PumpUntil(() => binaryDisplayed >= 10 && jsonDisplayed >= 10,
                    "Both frontends must display logs while writers stay open and rendering continues");
            }
            finally
            {
                writerTimer.Stop();
                reader.Stop();
            }
        }
        finally
        {
            Directory.Delete(directory, true);
        }
        TestHistory(dispatcher);
        TestHistorySelection();
        Console.WriteLine("PASS: bounded WPF log batches, live logs, history loading/timestamps and copying selected/filtered history and client/server live logs");
    }
}
