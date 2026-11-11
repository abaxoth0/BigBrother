namespace frontend.Services;

public static class ClientStartup
{
    public static async Task<bool> StartAsync(Func<Task<string?>> pendingRecovery,
        Action<string> launchRecovery, Func<Task> attachBackend)
    {
        var work = await pendingRecovery();
        if (work != null)
        {
            launchRecovery(work);
            return false;
        }
        await attachBackend();
        return true;
    }
}
