using frontend.Services;

internal static class StartupRecoveryTests
{
    public static async Task RunAsync()
    {
        var checkedRecovery = new TaskCompletionSource<string?>();
        bool attached = false;
        string? launched = null;
        var startup = ClientStartup.StartAsync(() => checkedRecovery.Task,
            work => launched = work, () => {
                attached = true;
                throw new IOException("Backend executable is damaged.");
            });
        if (attached || launched != null || startup.IsCompleted)
            throw new Exception("Startup did not wait for the recovery check.");
        checkedRecovery.SetResult("pending-rollback");
        if (await startup || attached || launched != "pending-rollback")
            throw new Exception("Recovery requires the damaged backend or opens the normal frontend.");

        try
        {
            await ClientStartup.StartAsync(() => Task.FromResult<string?>("pending-rollback"),
                _ => throw new IOException("Could not launch recovery."),
                () => { attached = true; return Task.CompletedTask; });
            throw new Exception("Failed recovery launch was ignored.");
        }
        catch (IOException) { }
        if (attached) throw new Exception("Backend started after a failed recovery launch.");

        launched = null;
        if (!await ClientStartup.StartAsync(() => Task.FromResult<string?>(null),
                work => launched = work, () => { attached = true; return Task.CompletedTask; }) ||
            !attached || launched != null)
            throw new Exception("Normal startup did not attach to the backend.");
        Console.WriteLine("PASS: startup recovery precedes backend access, works with a damaged backend and aborts on launch failure.");
    }
}
