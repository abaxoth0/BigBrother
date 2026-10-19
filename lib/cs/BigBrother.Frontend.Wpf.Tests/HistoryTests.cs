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
        var reply = Task.Run(async () =>
        {
            await server.WaitForConnectionAsync(timeout.Token);
            using var input = new StreamReader(server, leaveOpen: true);
            Check(await input.ReadLineAsync(timeout.Token) == "GET_LOG_PATH", "Unexpected history request");
            Check(await input.ReadLineAsync(timeout.Token) == "", "Missing request terminator");
            await uiResponsive.Task.WaitAsync(timeout.Token);
            int length = Encoding.UTF8.GetByteCount(path);
            await server.WriteAsync(Encoding.UTF8.GetBytes($"OK\n{length}\n{path}\n{length}\n{path}\n\n"), timeout.Token);
            await server.FlushAsync(timeout.Token);
        });

        // Isolate the history handler from the constructor's service subscriptions and timers.
        var vm = (MainViewModel)RuntimeHelpers.GetUninitializedObject(typeof(MainViewModel));
        typeof(MainViewModel).GetField("_ipcService", BindingFlags.Instance | BindingFlags.NonPublic)!.SetValue(vm, ipc);
        typeof(MainViewModel).GetField("_lastClientPid", BindingFlags.Instance | BindingFlags.NonPublic)!.SetValue(vm, 1);
        List<LogFileSource>? opened = null;
        vm.LogViewRequested += sources => opened = sources;
        var opening = vm.OpenHistoryAsync();
        Check(!opening.IsCompleted, "History request must yield while waiting for the pipe");
        dispatcher.BeginInvoke(DispatcherPriority.Normal, () => uiResponsive.TrySetResult());
        PumpUntil(() => opening.IsCompleted && reply.IsCompleted, "History request deadlocked the UI");
        opening.GetAwaiter().GetResult();
        reply.GetAwaiter().GetResult();
        Check(opened?.Count == 2 && opened.All(source => source.FilePath == path),
            "History must open both returned log sources");
        server.Dispose();
        opened = null;
        Check(vm.OpenHistoryAsync().IsCompleted && opened?.Count == 2,
            "Cached history paths must open without waiting for the backend");
    }
}
