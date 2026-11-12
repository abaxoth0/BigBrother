using frontend.Services;

internal static class StartupRecoveryTests
{
    public static async Task RunAsync()
    {
        var checkedRecovery = new TaskCompletionSource<string?>();
        bool attached = false;
        string? launched = null;
        var startup = ClientStartup.StartAsync(null, () => checkedRecovery.Task,
            work => launched = work, _ => {
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
            await ClientStartup.StartAsync(null, () => Task.FromResult<string?>("pending-rollback"),
                _ => throw new IOException("Could not launch recovery."),
                _ => { attached = true; return Task.CompletedTask; });
            throw new Exception("Failed recovery launch was ignored.");
        }
        catch (IOException) { }
        if (attached) throw new Exception("Backend started after a failed recovery launch.");

        launched = null;
        if (!await ClientStartup.StartAsync(null, () => Task.FromResult<string?>(null),
                work => launched = work, _ => {
                    attached = true;
                    throw new IOException("Service is stopped, missing or cannot start.");
                }) || attached || launched != null)
            throw new Exception("Normal startup requires or starts the backend.");

        if (!await ClientStartup.StartAsync(1234,
                () => throw new Exception("Update worker must not launch recovery."),
                _ => throw new Exception("Update worker must not launch recovery."),
                pid => {
                    if (pid != 1234) throw new Exception("Wrong worker backend PID.");
                    attached = true;
                    return Task.CompletedTask;
                }) || !attached)
            throw new Exception("Update worker did not attach to its owning backend.");
        try
        {
            await ClientStartup.StartAsync(1234, () => Task.FromResult<string?>(null), _ => { },
                _ => throw new IOException("Owning backend stopped."));
            throw new Exception("Update worker ignored its unavailable backend.");
        }
        catch (IOException) { }
        Console.WriteLine("PASS: offline frontend startup without service access, interrupted-update recovery and update-worker lifetime enforcement.");
    }
}
