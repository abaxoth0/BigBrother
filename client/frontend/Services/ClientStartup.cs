namespace frontend.Services;

public static class ClientStartup
{
    public static async Task<bool> StartAsync(int? workerBackendPid, Func<Task<string?>> pendingRecovery,
        Action<string> launchRecovery, Func<int, Task> attachWorker)
    {
        if (workerBackendPid is int pid)
        {
            await attachWorker(pid);
            return true;
        }
        var work = await pendingRecovery();
        if (work != null)
        {
            launchRecovery(work);
            return false;
        }
        return true;
    }
}
