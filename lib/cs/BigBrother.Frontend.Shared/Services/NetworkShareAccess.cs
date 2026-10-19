using System.ComponentModel;
using System.IO;
using System.Runtime.InteropServices;
using System.Text;
using Microsoft.Win32;

namespace frontend.Services;

public static class NetworkShareAccess
{
    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
    private struct NetworkResource
    {
        public int Scope, Type, DisplayType, Usage;
        public string? LocalName, RemoteName, Comment, Provider;
    }

    [DllImport("mpr.dll", CharSet = CharSet.Unicode)]
    private static extern int WNetGetConnection(string localName, StringBuilder remoteName, ref int length);
    [DllImport("mpr.dll", CharSet = CharSet.Unicode)]
    private static extern int WNetAddConnection3(IntPtr owner, ref NetworkResource resource, string? password, string? user, int flags);
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode)]
    private static extern uint GetDriveType(string root);

    public static string ToUnc(string path, Func<string, string?>? resolveDrive = null)
    {
        path = path.Trim().Replace('/', '\\');
        if (path.Length >= 3 && char.IsAsciiLetter(path[0]) && path[1] == ':' && path[2] == '\\')
        {
            var remote = (resolveDrive ?? ResolveDrive)(path[..2]);
            if (string.IsNullOrEmpty(remote))
                throw new IOException("Укажите сетевой путь \\\\сервер\\папка\\обновление.exe. Локальный диск или недоступную букву сетевого диска нельзя передать клиентам.");
            path = remote.TrimEnd('\\') + path[2..];
        }
        ShareRoot(path);
        return path;
    }

    public static string ShareRoot(string path)
    {
        if (!path.StartsWith(@"\\") || path.Length > 2048 || path.IndexOfAny(['/', ':', '*', '?', '"', '<', '>', '|']) >= 0 || path.Any(char.IsControl))
            throw new IOException("Укажите сетевой путь в формате \\\\сервер\\общая_папка.");
        var parts = path[2..].Split('\\');
        if (parts.Length < 2 || parts[0].Length == 0 || parts[1].Length == 0 ||
            parts[0].Any(char.IsWhiteSpace) || parts.Any(p => p is "." or ".." || p.EndsWith(' ') || p.EndsWith('.')) ||
            parts.Take(parts.Length - 1).Any(string.IsNullOrEmpty))
            throw new IOException("Некорректный сетевой путь. Укажите сервер и общую папку.");
        return @"\\" + parts[0] + "\\" + parts[1];
    }

    private static string? ResolveDrive(string drive)
    {
        if (!OperatingSystem.IsWindows()) return null;
        var length = 512;
        var remote = new StringBuilder(length);
        var error = WNetGetConnection(drive, remote, ref length);
        if (error == 234)
        {
            remote = new StringBuilder(length);
            error = WNetGetConnection(drive, remote, ref length);
        }
        if (error == 0) return remote.ToString();
        if (GetDriveType(drive + "\\") is 2 or 3 or 5 or 6) return null;
        using var key = Registry.CurrentUser.OpenSubKey(@"Network\" + char.ToUpperInvariant(drive[0]));
        return key?.GetValue("RemotePath") as string;
    }

    public static IReadOnlyList<string> SavedLocations()
    {
        var locations = new List<string>();
        for (var drive = 'A'; drive <= 'Z'; drive++)
        {
            var remote = ResolveDrive(drive + ":");
            if (string.IsNullOrEmpty(remote)) continue;
            try { locations.Add(ToUnc(remote)); }
            catch (IOException) { }
        }
        return locations.Distinct(StringComparer.OrdinalIgnoreCase).ToArray();
    }

    public static async Task<T> ReadAsync<T>(string path, Func<T> read, Func<string, Task> authenticate)
    {
        try { return await Task.Run(read); }
        catch (Exception ex) when (path.StartsWith(@"\\") && NeedsCredentials(ex))
        {
            await authenticate(ShareRoot(path));
            return await Task.Run(read);
        }
    }

    private static bool NeedsCredentials(Exception ex) => ex is UnauthorizedAccessException ||
        (ex is IOException or Win32Exception && (ex is Win32Exception win32 ? win32.NativeErrorCode : ex.HResult & 0xffff)
            is 5 or 65 or 86 or 1244 or 1326 or 1327 or 1328 or 1329 or 1330 or 1331 or 1907 or 2202);

    public static void Connect(string share, IntPtr owner)
    {
        var resource = new NetworkResource { Type = 1, RemoteName = ShareRoot(share) };
        var error = WNetAddConnection3(owner, ref resource, null, null, 0x08 | 0x10);
        if (error == 0) return;
        if (error == 1223) throw new OperationCanceledException("Подключение к общей папке отменено.");
        if (error == 1219)
            throw new IOException("Windows уже подключена к этому серверу с другой учётной записью. Используйте ту же запись или завершите конфликтующее подключение вручную.");
        throw new IOException($"Не удалось подключиться к {share}: {new Win32Exception(error).Message} (Windows {error}).");
    }
}
