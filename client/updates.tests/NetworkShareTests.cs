using System.ComponentModel;
using frontend.Services;

public static class NetworkShareTests
{
    private static void Assert(bool condition, string message)
    {
        if (!condition) throw new Exception(message);
    }

    public static async Task RunAsync()
    {
        const string package = @"\\fileserver\updates\release\Client Update.exe";
        var published = NetworkShareAccess.ToUnc(@"Y:\release\Client Update.exe", drive => drive == "Y:" ? @"\\fileserver\updates" : null);
        var clientPath = NetworkShareAccess.ToUnc(@"Z:\release\Client Update.exe", drive => drive == "Z:" ? @"\\fileserver\updates" : null);
        Assert(published == package && published == clientPath, "Different mapped drive letters must refer to the same published UNC path.");
        Assert(NetworkShareAccess.ToUnc(package, _ => throw new Exception("UNC paths must not look up local drive mappings.")) == package, "UNC path changed.");
        Assert(NetworkShareAccess.ShareRoot(package) == @"\\fileserver\updates", "Authentication must target the share, not the executable.");
        Assert(NetworkShareAccess.ShareRoot(@"\\fileserver\updates\") == @"\\fileserver\updates", "A share root with a trailing separator was rejected.");
        Assert(NetworkShareAccess.ToUnc(" //fileserver/updates/Client Update.exe ") == @"\\fileserver\updates\Client Update.exe", "Typed separators were not normalized.");
        foreach (var path in new[] { @"C:\local\update.exe", @"Q:\update.exe", "relative.exe", @"\\fileserver", @"\\fileserver\", @"\\.\C$\update.exe",
            @"\\?\UNC\fileserver\updates\update.exe", @"\\fileserver\updates\..\update.exe", @"\\fileserver\updates\update.exe:stream", "\\\\fileserver\\updates\\bad\n.exe" })
        {
            try { NetworkShareAccess.ToUnc(path, _ => null); }
            catch (IOException) { continue; }
            throw new Exception("Unsafe or machine-specific update path accepted: " + path);
        }

        var prompts = 0;
        Task Authenticate(string share)
        {
            Assert(share == @"\\fileserver\updates", "Credentials were requested for the wrong share.");
            prompts++;
            return Task.CompletedTask;
        }
        Assert(await NetworkShareAccess.ReadAsync(package, () => 42, Authenticate) == 42 && prompts == 0,
            "An accessible package must not prompt for credentials.");
        var reads = 0;
        var result = await NetworkShareAccess.ReadAsync(package, () => {
            if (++reads == 1) throw new Win32Exception(1326);
            return 42;
        }, Authenticate);
        Assert(result == 42 && reads == 2 && prompts == 1, "A failed login must authenticate once and retry the read.");
        reads = 0;
        result = await NetworkShareAccess.ReadAsync(package, () => {
            if (++reads == 1) throw new IOException("Logon failed", unchecked((int)0x8007052e));
            return 42;
        }, Authenticate);
        Assert(result == 42 && reads == 2 && prompts == 2, "File-open HRESULTs must also trigger credential retry.");

        foreach (var error in new Exception[] { new FileNotFoundException(), new Win32Exception(53), new IOException("Hash mismatch") })
        {
            try { await NetworkShareAccess.ReadAsync<int>(package, () => throw error, Authenticate); }
            catch (Exception ex) when (ReferenceEquals(ex, error)) { continue; }
            throw new Exception("An unrelated read failure was hidden.");
        }
        Assert(prompts == 2, "Missing packages and offline servers must not cause credential prompts.");
        reads = 0;
        try
        {
            await NetworkShareAccess.ReadAsync<int>(package, () => { reads++; throw new UnauthorizedAccessException(); }, Authenticate);
            throw new Exception("Persistent access denial was hidden.");
        }
        catch (UnauthorizedAccessException) { }
        Assert(reads == 2 && prompts == 3, "Persistent access denial must not loop through credential dialogs.");

        reads = 0;
        try
        {
            await NetworkShareAccess.ReadAsync<int>(package, () => { reads++; throw new UnauthorizedAccessException(); }, _ => throw new OperationCanceledException());
            throw new Exception("Cancelled authentication was ignored.");
        }
        catch (OperationCanceledException) { }
        Assert(reads == 1, "Cancelled authentication must not retry the read.");
        Console.WriteLine("PASS: UNC paths across different drive letters, share authentication retries, cancellation and offline/missing-file handling.");
    }
}
