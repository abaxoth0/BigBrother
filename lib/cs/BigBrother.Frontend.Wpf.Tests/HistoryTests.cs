using System.IO;
using System.IO.Pipes;
using System.Reflection;
using System.Runtime.CompilerServices;
using System.Text;
using System.Windows.Threading;
using frontend.Models;
using frontend.Services;
using frontend.ViewModels;

internal static partial class Program
{
    private static void TestHistory(Dispatcher dispatcher)
    {
        string directory = Path.Combine(Path.GetTempPath(), "bigbrother-history-tests-" + Guid.NewGuid());
        Directory.CreateDirectory(directory);
        try
        {
            string path = Path.Combine(directory, "client.binlog");
            using (var output = File.Create(path))
            {
                var record = Record("history " + new string('x', 200));
                for (int i = 0; i < 50000; i++) output.Write(record);
                output.Write(Record("history tail"));
            }
            var sources = new List<LogFileSource> { new() { Label = "Client", FilePath = path } };
            using (var history = new LogViewViewModel(sources))
            {
                Check(history.Sources[0].AllEntries.Count == 0, "History construction must not read files");
                bool wrongThread = false;
                int published = 0;
                history.ReloadRequested += () =>
                {
                    wrongThread |= !dispatcher.CheckAccess();
                    published++;
                };
                var loading = history.ReloadAsync();
                Check(!loading.IsCompleted && history.IsLoading, "Large history load must yield to the UI");
                Check(!history.ReloadCommand.CanExecute(null) && !history.ExportCommand.CanExecute(null),
                    "Reload/export must wait for the complete history");
                Check(history.ReloadAsync().IsCompleted, "Repeated reload must not start another load");
                history.SearchText = "TAIL";
                PumpUntil(() => loading.IsCompleted, "Large history load did not complete");
                loading.GetAwaiter().GetResult();
                Check(!history.IsLoading && !wrongThread, "History results must be published on the UI thread");
                Check(history.Sources[0].AllEntries.Count == 50001, "History must retain every record");
                Check(history.Sources[0].FilteredEntries.Single().Message == "history tail",
                    "Loading must apply the latest case-insensitive search");
                Check(published == 2, "Search and completed load each publish once");
            }
            using (var history = new LogViewViewModel(sources))
            {
                int published = 0;
                history.ReloadRequested += () => published++;
                var loading = history.ReloadAsync();
                history.Dispose();
                PumpUntil(() => loading.IsCompleted, "Closing history must cancel the load");
                loading.GetAwaiter().GetResult();
                Check(published == 0 && history.Sources[0].AllEntries.Count == 0,
                    "Closed history must not publish late results");
            }

            TestHistoryRequest(dispatcher, path);
        }
        finally
        {
            Directory.Delete(directory, true);
        }
    }

    private static void TestHistoryRequest(Dispatcher dispatcher, string path)
    {
        string pipeName = "BigBrother.History.Test." + Guid.NewGuid().ToString("N");
        using var server = new NamedPipeServerStream(pipeName, PipeDirection.InOut, 1,
            PipeTransmissionMode.Byte, PipeOptions.Asynchronous);
        using var timeout = new CancellationTokenSource(TimeSpan.FromSeconds(5));
        using var ipc = (IpcService)Activator.CreateInstance(typeof(IpcService),
            BindingFlags.Instance | BindingFlags.NonPublic, null, [pipeName], null)!;
        var uiResponsive = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        var record = Record("broker history");
        var reply = Task.Run(async () =>
        {
            await server.WaitForConnectionAsync(timeout.Token);
            using var input = new StreamReader(server, leaveOpen: true);
            Check(await input.ReadLineAsync(timeout.Token) == "READ_LOG", "History must read through the broker");
            foreach (var argument in new[] { "firewall", "0", "" })
            {
                Check(await input.ReadLineAsync(timeout.Token) == argument.Length.ToString(), "Incorrect log argument length");
                Check(await input.ReadLineAsync(timeout.Token) == argument, "Incorrect log argument");
            }
            Check(await input.ReadLineAsync(timeout.Token) == "", "Missing request terminator");
            await uiResponsive.Task.WaitAsync(timeout.Token);
            var fields = new[] { "test", record.Length.ToString(), record.Length.ToString(), Convert.ToHexString(record) };
            string payload = "OK\n" + string.Concat(fields.Select(field => field.Length + "\n" + field + "\n")) + "\n";
            await server.WriteAsync(Encoding.UTF8.GetBytes(payload), timeout.Token);
            await server.FlushAsync(timeout.Token);
        });

        var vm = (MainViewModel)RuntimeHelpers.GetUninitializedObject(typeof(MainViewModel));
        typeof(MainViewModel).GetField("_ipcService", BindingFlags.Instance | BindingFlags.NonPublic)!.SetValue(vm, ipc);
        List<LogFileSource>? opened = null;
        vm.LogViewRequested += sources => opened = sources;
        Check(vm.OpenHistoryAsync().IsCompleted, "Opening history must not wait for backend I/O");
        Check(opened?.Count == 2 && opened.All(source => source.LoadAsync != null && source.FilePath == ""),
            "History sources must use broker readers rather than direct file paths");
        var loading = opened![0].LoadAsync!(timeout.Token);
        Check(!loading.IsCompleted, "Broker history must yield while waiting for the pipe");
        dispatcher.BeginInvoke(DispatcherPriority.Normal, () => uiResponsive.TrySetResult());
        PumpUntil(() => loading.IsCompleted && reply.IsCompleted, "Broker history deadlocked the UI");
        reply.GetAwaiter().GetResult();
        Check(loading.GetAwaiter().GetResult().Single().Message == "broker history", "Broker history lost log records");
    }
}
