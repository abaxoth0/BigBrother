using System.Diagnostics;
using System.IO;
using System.Runtime.InteropServices;
using System.Security.AccessControl;
using System.Security.Principal;
using System.ServiceProcess;
using System.Text.Json;

namespace frontend.Services;

public sealed class UpdateTransaction
{
    public string Install { get; set; } = "";
    public string Phase { get; set; } = "downloaded";
    public bool WasRunning { get; set; }
    public int ParentPid { get; set; }
    public ClientUpdate Update { get; set; } = null!;
    public UpdateSnapshot? Snapshot { get; set; }
}

public static class ClientUpdates
{
    private const string PrivateAcl = "O:BAG:BAD:P(A;OICI;FA;;;SY)(A;OICI;FA;;;BA)";
    public static string Root => Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.CommonApplicationData), "BigBrotherClientUpdates");
    private static readonly string[] PayloadFiles = {
        "BigBrother Notifications.exe", "BigBrother Client Daemon.exe", "BigBrother Firewall.exe",
        "WinDivert.dll", "WinDivert64.sys", "client-version.txt", Path.Combine("GUI", "BigBrother Client.exe"),
        Path.Combine("GUI", "D3DCompiler_47_cor3.dll"), Path.Combine("GUI", "PenImc_cor3.dll"),
        Path.Combine("GUI", "PresentationNative_cor3.dll"), Path.Combine("GUI", "vcruntime140_cor3.dll"),
        Path.Combine("GUI", "wpfgfx_cor3.dll")
    };

    [DllImport("kernel32.dll", CharSet = CharSet.Unicode)]
    private static extern uint GetDriveType(string root);
    [StructLayout(LayoutKind.Sequential)]
    private struct TokenPrivilege { public uint Count; public uint Low; public int High; public uint Attributes; }
    [DllImport("advapi32.dll", SetLastError = true)]
    private static extern bool OpenProcessToken(IntPtr process, uint access, out Microsoft.Win32.SafeHandles.SafeAccessTokenHandle token);
    [DllImport("advapi32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    private static extern bool LookupPrivilegeValue(string? system, string name, out long luid);
    [DllImport("advapi32.dll", SetLastError = true)]
    private static extern bool AdjustTokenPrivileges(Microsoft.Win32.SafeHandles.SafeAccessTokenHandle token, bool disable,
        ref TokenPrivilege privileges, uint length, IntPtr previous, IntPtr returned);

    public static void RequireLocal(string path)
    {
        var full = Path.GetFullPath(path);
        if (full.Length < 3 || full[1] != ':' || full[2] != '\\' || GetDriveType(full[..3]) != 3)
            throw new IOException("Updates must run from a local fixed disk.");
        UpdateSnapshot.RejectLinks(full);
    }

    private static void RequireAdmin()
    {
        using var identity = WindowsIdentity.GetCurrent();
        if (!new WindowsPrincipal(identity).IsInRole(WindowsBuiltInRole.Administrator))
            throw new UnauthorizedAccessException("Administrator rights are required to update.");
        if (!OpenProcessToken(Process.GetCurrentProcess().Handle, 0x28, out var token))
            throw new System.ComponentModel.Win32Exception(Marshal.GetLastWin32Error());
        using (token)
        {
            if (!LookupPrivilegeValue(null, "SeRestorePrivilege", out var luid))
                throw new System.ComponentModel.Win32Exception(Marshal.GetLastWin32Error());
            var privileges = new TokenPrivilege { Count = 1, Low = (uint)luid, High = (int)(luid >> 32), Attributes = 2 };
            if (!AdjustTokenPrivileges(token, false, ref privileges, 0, IntPtr.Zero, IntPtr.Zero) || Marshal.GetLastWin32Error() != 0)
                throw new UnauthorizedAccessException("Cannot enable the privilege needed to restore file ownership.");
        }
    }

    private static void PrivateDirectory(string path)
    {
        RequireLocal(path);
        if (Directory.Exists(path)) AssertPrivate(path, true);
        var security = new DirectorySecurity();
        security.SetSecurityDescriptorSddlForm(PrivateAcl);
        var directory = new DirectoryInfo(path);
        directory.Create(security);
        directory.SetAccessControl(security);
    }

    private static void AssertPrivate(string path, bool directory, bool requireProtected = true)
    {
        RequireLocal(path);
        FileSystemSecurity security = directory ? new DirectoryInfo(path).GetAccessControl() : new FileInfo(path).GetAccessControl();
        var owner = security.GetOwner(typeof(SecurityIdentifier))?.Value;
        if (owner is not ("S-1-5-18" or "S-1-5-32-544"))
            throw new IOException("The update staging owner is not SYSTEM or Administrators.");
        var trustedAllow = false;
        foreach (FileSystemAccessRule rule in security.GetAccessRules(true, true, typeof(SecurityIdentifier)))
        {
            if (rule.AccessControlType == AccessControlType.Allow && rule.IdentityReference.Value is not ("S-1-5-18" or "S-1-5-32-544"))
                throw new IOException("The update staging DACL allows an untrusted principal.");
            if (rule.AccessControlType == AccessControlType.Allow) trustedAllow = true;
        }
        if (!trustedAllow || (requireProtected && !security.AreAccessRulesProtected))
            throw new IOException("The update staging DACL is missing or inherits permissions.");
    }

    private static FileStream CreatePrivateFile(string path)
    {
        RequireLocal(path);
        var security = new FileSecurity();
        security.SetSecurityDescriptorSddlForm("O:BAG:BAD:P(A;;FA;;;SY)(A;;FA;;;BA)");
        return new FileInfo(path).Create(FileMode.CreateNew, FileSystemRights.FullControl,
            FileShare.None, 65536, FileOptions.WriteThrough, security);
    }

    private static void PrivateFile(string path)
    {
        RequireLocal(path);
        var security = new FileSecurity();
        security.SetSecurityDescriptorSddlForm("O:BAG:BAD:P(A;;FA;;;SY)(A;;FA;;;BA)");
        new FileInfo(path).SetAccessControl(security);
    }

    private static string ReadSecurity(string path, bool directory) => directory
        ? new DirectoryInfo(path).GetAccessControl().GetSecurityDescriptorSddlForm(AccessControlSections.Access | AccessControlSections.Owner | AccessControlSections.Group)
        : new FileInfo(path).GetAccessControl().GetSecurityDescriptorSddlForm(AccessControlSections.Access | AccessControlSections.Owner | AccessControlSections.Group);

    private static void RestoreSecurity(string path, bool directory, string descriptor)
    {
        if (directory)
        {
            var security = new DirectorySecurity();
            security.SetSecurityDescriptorSddlForm(descriptor);
            new DirectoryInfo(path).SetAccessControl(security);
        }
        else
        {
            var security = new FileSecurity();
            security.SetSecurityDescriptorSddlForm(descriptor);
            new FileInfo(path).SetAccessControl(security);
        }
    }

    private static string InstallDirectory => Path.GetFullPath(Path.Combine(AppContext.BaseDirectory, ".."));
    public static string InstalledVersion
    {
        get
        {
            var path = Path.Combine(InstallDirectory, "client-version.txt");
            return System.IO.File.Exists(path) ? System.IO.File.ReadAllText(path).Trim() : "0.0.0";
        }
    }

    public static async Task<ClientUpdate?> CheckAsync(IpcService ipc)
    {
        var json = await ipc.GetClientUpdateAsync();
        if (string.IsNullOrEmpty(json)) return null;
        var update = JsonSerializer.Deserialize<ClientUpdate>(json) ?? throw new IOException("Отсутствуют сведения об обновлении.");
        update.Validate();
        return update;
    }

    private static string Journal(string work) => Path.Combine(work, "transaction.json");

    public static string? PendingRecovery()
    {
        RequireAdmin();
        PrivateDirectory(Root);
        foreach (var directory in Directory.EnumerateDirectories(Root))
        {
            RequireLocal(directory);
            var journal = Journal(directory);
            if (!System.IO.File.Exists(journal)) continue;
            RequireLocal(journal);
            AssertPrivate(directory, true);
            AssertPrivate(journal, false);
            var transaction = JsonSerializer.Deserialize<UpdateTransaction>(System.IO.File.ReadAllText(journal));
            if (transaction?.Phase is "backing-up" or "applying" or "restoring") return directory;
        }
        return null;
    }

    public static async Task<string> PrepareAsync(IpcService ipc, ClientUpdate approved, Action<string> progress)
    {
        RequireAdmin();
        RequireLocal(InstallDirectory);
        approved.Validate();
        var update = await CheckAsync(ipc);
        if (update == null || !approved.SamePackage(update))
            throw new IOException("Обновление на сервере изменилось. Проверьте обновления ещё раз.");
        if (!update.IsNewerThan(InstalledVersion)) throw new IOException("Установлена актуальная версия.");
        if (PendingRecovery() != null) throw new IOException("Сначала необходимо восстановить прерванное обновление. Перезапустите клиент.");
        PrivateDirectory(Root);
        var work = Path.Combine(Root, Guid.NewGuid().ToString("N"));
        PrivateDirectory(work);
        try
        {
            var package = Path.Combine(work, "package.exe");
            progress("Загрузка обновления…");
            await using (var input = new FileStream(update.Location, FileMode.Open, FileAccess.Read, FileShare.Read, 65536, true))
            await using (var output = CreatePrivateFile(package))
            {
                var buffer = new byte[65536];
                while (output.Length < update.Size)
                {
                    var count = await input.ReadAsync(buffer);
                    if (count == 0 || output.Length + count > update.Size)
                        throw new IOException("Invalid update download length.");
                    await output.WriteAsync(buffer.AsMemory(0, count));
                    progress($"Загрузка обновления: {output.Length * 100 / update.Size}%");
                }
                if (input.ReadByte() != -1) throw new IOException("Invalid update download length.");
                output.Flush(true);
            }
            if (UpdateSnapshot.Hash(package) != update.Sha256) throw new IOException("Update SHA-256 mismatch.");
            PrivateFile(package);
            var runner = Path.Combine(work, "runner");
            PrivateDirectory(runner);
            foreach (var file in Directory.EnumerateFiles(AppContext.BaseDirectory))
            {
                RequireLocal(file);
                using (var input = System.IO.File.OpenRead(file))
                using (var output = CreatePrivateFile(Path.Combine(runner, Path.GetFileName(file))))
                { input.CopyTo(output); output.Flush(true); }
                PrivateFile(Path.Combine(runner, Path.GetFileName(file)));
            }
            UpdateSnapshot.WriteJson(Journal(work), new UpdateTransaction {
                Install = InstallDirectory, Update = update, ParentPid = Environment.ProcessId
            }, CreatePrivateFile);
            return work;
        }
        catch
        {
            Directory.Delete(work, true);
            throw;
        }
    }

    public static void Launch(string work)
    {
        RequireAdmin();
        RequireLocal(work);
        var executable = Path.Combine(work, "runner", "BigBrother Client.exe");
        RequireLocal(executable);
        AssertPrivate(executable, false);
        _ = Process.Start(new ProcessStartInfo(executable) {
            UseShellExecute = true, Verb = "runas", Arguments = $"--apply-update \"{work}\"", WorkingDirectory = work
        }) ?? throw new IOException("Could not start the elevated updater.");
    }

    private static void WaitForService(ServiceController service, ServiceControllerStatus status)
        => service.WaitForStatus(status, TimeSpan.FromSeconds(45));

    private static void StopService(ServiceController service)
    {
        service.Refresh();
        if (service.Status == ServiceControllerStatus.Stopped) return;
        if (service.Status != ServiceControllerStatus.StopPending) service.Stop();
        WaitForService(service, ServiceControllerStatus.Stopped);
    }

    private static void RestoreServiceState(ServiceController service, bool running)
    {
        service.Refresh();
        if (running && service.Status == ServiceControllerStatus.Running) return;
        StopService(service);
        if (running) { service.Start(); WaitForService(service, ServiceControllerStatus.Running); }
    }

    private static void CloseInstalledProcesses(string install)
    {
        foreach (var name in new[] { "BigBrother Client", "BigBrother Client Daemon", "BigBrother Firewall", "BigBrother Notifications" })
        foreach (var process in Process.GetProcessesByName(name))
        {
            using (process)
            {
                if (process.Id == Environment.ProcessId) continue;
                try
                {
                    var path = process.MainModule?.FileName;
                    if (path == null || !Path.GetFullPath(path).StartsWith(install.TrimEnd('\\') + "\\", StringComparison.OrdinalIgnoreCase)) continue;
                    process.CloseMainWindow();
                    if (!process.WaitForExit(30000)) throw new IOException("An installed client process is still running.");
                }
                catch (InvalidOperationException) { }
            }
        }
    }

    public static async Task ApplyAsync(string work, Action<string> progress)
    {
        RequireAdmin();
        RequireLocal(Environment.ProcessPath!);
        RequireLocal(work);
        if (!string.Equals(Path.GetDirectoryName(work), Root, StringComparison.OrdinalIgnoreCase))
            throw new IOException("Invalid update staging directory.");
        PrivateDirectory(Root);
        PrivateDirectory(work);
        var lockPath = Path.Combine(Root, "update.lock");
        RequireLocal(lockPath);
        if (!System.IO.File.Exists(lockPath)) CreatePrivateFile(lockPath).Dispose();
        AssertPrivate(lockPath, false);
        using var updateLock = new FileStream(lockPath, FileMode.OpenOrCreate, FileAccess.ReadWrite, FileShare.None);
        try { await ApplyTransactionAsync(work, progress); }
        catch
        {
            var transaction = JsonSerializer.Deserialize<UpdateTransaction>(System.IO.File.ReadAllText(Journal(work)));
            if (transaction?.Phase == "downloaded")
            {
                transaction.Phase = "failed";
                UpdateSnapshot.WriteJson(Journal(work), transaction, CreatePrivateFile);
            }
            throw;
        }
    }

    private static async Task ApplyTransactionAsync(string work, Action<string> progress)
    {
        var transaction = JsonSerializer.Deserialize<UpdateTransaction>(System.IO.File.ReadAllText(Journal(work)))
            ?? throw new IOException("Missing update transaction.");
        transaction.Update.Validate();
        RequireLocal(transaction.Install);
        if ((Root + "\\").StartsWith(transaction.Install.TrimEnd('\\') + "\\", StringComparison.OrdinalIgnoreCase) ||
            (transaction.Install + "\\").StartsWith(Root.TrimEnd('\\') + "\\", StringComparison.OrdinalIgnoreCase))
            throw new IOException("Installation and update staging directories must not overlap.");
        if (transaction.Phase == "downloaded" && !System.IO.File.Exists(Path.Combine(transaction.Install, "BigBrother Firewall.exe")))
            throw new IOException("The client installation is missing.");
        var backup = Path.Combine(work, "backup");
        using var service = new ServiceController("BigBrother Firewall");
        AssertPrivate(Journal(work), false);
        void Save() => UpdateSnapshot.WriteJson(Journal(work), transaction, CreatePrivateFile);
        async Task Restore()
        {
            if (transaction.Snapshot == null) throw new IOException("Missing update backup.");
            progress("Восстановление предыдущей версии…");
            transaction.Phase = "restoring";
            Save();
            StopService(service);
            CloseInstalledProcesses(transaction.Install);
            transaction.Snapshot.Restore(transaction.Install, backup, RestoreSecurity, CreatePrivateFile);
            RestoreServiceState(service, transaction.WasRunning);
            transaction.Phase = "restored";
            Save();
            await Task.CompletedTask;
        }
        if (transaction.Phase == "backing-up")
        {
            RestoreServiceState(service, transaction.WasRunning);
            transaction.Phase = "restored";
            Save();
            return;
        }
        if (transaction.Phase is "applying" or "restoring") { await Restore(); return; }
        if (transaction.Phase != "downloaded") throw new IOException("This update transaction has already finished.");
        var package = Path.Combine(work, "package.exe");
        RequireLocal(package);
        if (new FileInfo(package).Length != transaction.Update.Size || UpdateSnapshot.Hash(package) != transaction.Update.Sha256)
            throw new IOException("Update package verification failed.");
        var payload = Path.Combine(work, "payload");
        PrivateDirectory(payload);
        var temporary = Path.Combine(work, "temp");
        PrivateDirectory(temporary);
        progress("Подготовка обновления…");
        using (var packageLock = new FileStream(package, FileMode.Open, FileAccess.Read, FileShare.Read))
        using (var extraction = Process.Start(new ProcessStartInfo(package) {
            UseShellExecute = false, WorkingDirectory = work,
            Environment = { ["TEMP"] = temporary, ["TMP"] = temporary },
            Arguments = $"/EXTRACTONLY=1 /DIR=\"{payload}\" /VERYSILENT /SUPPRESSMSGBOXES /NORESTART /SP- /LOG=\"{Path.Combine(work, "extract.log")}\""
        }) ?? throw new IOException("Could not extract the update."))
        {
            using var timeout = new CancellationTokenSource(TimeSpan.FromMinutes(10));
            try { await extraction.WaitForExitAsync(timeout.Token); }
            catch (OperationCanceledException) { extraction.Kill(true); extraction.WaitForExit(); throw new IOException("Update extraction timed out."); }
            if (extraction.ExitCode != 0) throw new IOException("Update extraction failed: " + extraction.ExitCode);
        }
        var files = Directory.GetFiles(payload, "*", SearchOption.AllDirectories);
        if (files.Length != PayloadFiles.Length || PayloadFiles.Any(f => !System.IO.File.Exists(UpdateSnapshot.SafePath(payload, f))))
            throw new IOException("The update payload is incomplete or contains unexpected files.");
        foreach (var directory in Directory.EnumerateDirectories(payload, "*", SearchOption.AllDirectories))
        {
            AssertPrivate(directory, true, false);
            var security = new DirectorySecurity();
            security.SetSecurityDescriptorSddlForm(PrivateAcl);
            new DirectoryInfo(directory).SetAccessControl(security);
        }
        foreach (var file in files) { AssertPrivate(file, false, false); PrivateFile(file); }
        if (System.IO.File.ReadAllText(Path.Combine(payload, "client-version.txt")).Trim() != transaction.Update.Version)
            throw new IOException("Update payload version mismatch.");
        if (transaction.ParentPid != 0)
        {
            try
            {
                using var parent = Process.GetProcessById(transaction.ParentPid);
                if (!parent.WaitForExit(30000)) throw new IOException("Close the client before updating.");
            }
            catch (ArgumentException) { }
        }
        service.Refresh();
        transaction.WasRunning = service.Status != ServiceControllerStatus.Stopped;
        try
        {
            progress("Создание резервной копии…");
            transaction.Phase = "backing-up";
            Save();
            StopService(service);
            CloseInstalledProcesses(transaction.Install);
            PrivateDirectory(backup);
            transaction.Snapshot = UpdateSnapshot.Capture(transaction.Install, backup, ReadSecurity, PrivateDirectory, CreatePrivateFile);
            transaction.Snapshot.Verify(transaction.Install, backup);
            transaction.Phase = "applying";
            Save();
            progress("Установка обновления…");
            foreach (var name in PayloadFiles)
            {
                var target = UpdateSnapshot.SafePath(transaction.Install, name);
                var previous = transaction.Snapshot.Files.FirstOrDefault(f => f.Name.Equals(name, StringComparison.OrdinalIgnoreCase));
                if (previous != null && previous.Hash == UpdateSnapshot.Hash(UpdateSnapshot.SafePath(payload, name))) continue;
                UpdateSnapshot.Replace(UpdateSnapshot.SafePath(payload, name), target, CreatePrivateFile,
                    path => {
                        if (previous != null) RestoreSecurity(path, false, previous.Security);
                        else
                        {
                            var security = new FileSecurity();
                            security.SetSecurityDescriptorSddlForm("O:BAG:BAD:P(A;;FA;;;SY)(A;;FA;;;BA)(A;;FRFX;;;BU)");
                            new FileInfo(path).SetAccessControl(security);
                        }
                    });
            }
            progress("Проверка запуска…");
            service.Start();
            WaitForService(service, ServiceControllerStatus.Running);
            using var ipc = new IpcService();
            var healthy = false;
            for (var attempt = 0; attempt < 30; attempt++)
            {
                var status = await ipc.GetStatusAsync();
                service.Refresh();
                if (status?.DaemonStatus == "RUNNING" && service.Status == ServiceControllerStatus.Running) { healthy = true; break; }
                await Task.Delay(1000);
            }
            if (!healthy) throw new IOException("The updated client did not become ready.");
            if (!transaction.WasRunning) StopService(service);
            transaction.Phase = "complete";
            Save();
        }
        catch (Exception failure)
        {
            if (transaction.Phase is "applying" or "restoring" or "complete")
            {
                try { await Restore(); }
                catch (Exception restoreFailure) { throw new IOException($"Update failed: {failure.Message}\nRecovery remains pending: {restoreFailure.Message}\nBackup: {backup}"); }
                throw new IOException("Update failed; the previous installation was restored.\n" + failure.Message);
            }
            RestoreServiceState(service, transaction.WasRunning);
            transaction.Phase = "restored";
            Save();
            throw;
        }
    }
}
