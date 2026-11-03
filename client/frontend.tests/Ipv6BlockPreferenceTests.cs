using System.IO.Pipes;
using System.Text;
using frontend.Services;
using frontend.ViewModels;

internal static class Ipv6BlockPreferenceTests
{
    public static async Task RunAsync()
    {
        var preference = new Ipv6BlockPreference();
        var pipeName = "BigBrother.Ipv6Preference.Test." + Guid.NewGuid().ToString("N");
        async Task<bool> Refresh(string reply)
        {
            using var pipe = new NamedPipeServerStream(pipeName, PipeDirection.InOut, 1,
                PipeTransmissionMode.Byte, PipeOptions.Asynchronous);
            using var timeout = new CancellationTokenSource(TimeSpan.FromSeconds(5));
            using var service = new IpcService(pipeName);
            var revision = preference.Revision;
            var request = service.GetIpv6BlockAsync();
            await pipe.WaitForConnectionAsync(timeout.Token);
            using var reader = new StreamReader(pipe, leaveOpen: true);
            Check(await reader.ReadLineAsync(timeout.Token) == "GET_IPV6_BLOCK" &&
                await reader.ReadLineAsync(timeout.Token) == "", "Incorrect IPv6 preference request");
            await pipe.WriteAsync(Encoding.UTF8.GetBytes(reply), timeout.Token);
            await pipe.FlushAsync(timeout.Token);
            return preference.Refresh(await request, revision);
        }

        Check(await Refresh("OK\n1\n0\n\n") && !preference.Value && !preference.IsEdited,
            "Server disable did not update an untouched checkbox");
        Check(await Refresh("OK\n1\n1\n\n") && preference.Value && !preference.IsEdited,
            "Server enable did not update the checkbox or created a settings draft");
        Check(!await Refresh("ERROR\n11\nunavailable\n\n") && preference.Value && !preference.IsEdited,
            "Failed preference retrieval cleared the checkbox");
        Check(preference.Edit(false) && preference.IsEdited, "Checkbox edit did not create a draft");
        Check(!await Refresh("OK\n1\n1\n\n") && !preference.Value && preference.IsEdited,
            "State refresh discarded an unsaved checkbox edit");

        var beforeSave = preference.Revision;
        preference.ConfirmSave(beforeSave);
        Check(!preference.IsEdited && !preference.Refresh(true, beforeSave) && !preference.Value,
            "A read from before the save overwrote the acknowledged value");
        Check(await Refresh("OK\n1\n1\n\n") && preference.Value && !preference.IsEdited,
            "Checkbox stopped following server changes after saving");

        var beforeEdit = preference.Revision;
        preference.Edit(false);
        Check(!preference.Refresh(true, beforeEdit) && !preference.Value,
            "An in-flight read replaced a later edit");
        var saving = preference.Revision;
        preference.Edit(true);
        preference.ConfirmSave(saving);
        Check(preference.IsEdited && preference.Value, "An earlier save cleared a newer edit");
        preference.ConfirmSave(preference.Revision);
        Check(!preference.Edit(true) && !preference.IsEdited && !await Refresh("OK\n1\n1\n\n"),
            "Unchanged settings caused edits or redundant checkbox updates");
        Check(await Refresh("OK\n1\n0\n\n") && !preference.Value,
            "Checkbox stopped following the server after repeated edits and saves");
        Console.WriteLine("IPv6 settings tests passed: live changes, drafts, failed reads and stale edit/save responses.");
    }

    private static void Check(bool condition, string message)
    {
        if (!condition) throw new Exception(message);
    }
}
