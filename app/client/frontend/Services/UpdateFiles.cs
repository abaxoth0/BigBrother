using System.IO;
using System.Security.Cryptography;
using System.Text.Json;

namespace frontend.Services;

public sealed record UpdateFile(string Name, bool Directory, int Attributes, long Written,
    string Hash, string Security);

public sealed class UpdateSnapshot
{
    public List<UpdateFile> Files { get; set; } = new();

    public static string SafePath(string root, string name)
    {
        var path = Path.GetFullPath(Path.Combine(root, name));
        var prefix = Path.GetFullPath(root).TrimEnd(Path.DirectorySeparatorChar) + Path.DirectorySeparatorChar;
        if (!path.StartsWith(prefix, StringComparison.OrdinalIgnoreCase) || name.Contains(':'))
            throw new IOException("Invalid update path.");
        RejectLinks(path);
        return path;
    }

    public static void RejectLinks(string path)
    {
        for (var current = Path.GetFullPath(path); current != null; current = Path.GetDirectoryName(current))
        {
            if ((File.Exists(current) || Directory.Exists(current)) &&
                (File.GetAttributes(current) & FileAttributes.ReparsePoint) != 0)
                throw new IOException("Update paths cannot contain links or junctions.");
        }
    }

    public static string Hash(string file)
    {
        using var stream = File.OpenRead(file);
        return Convert.ToHexStringLower(SHA256.HashData(stream));
    }

    public static void WriteJson<T>(string path, T value, Func<string, FileStream>? create = null)
    {
        var temporary = path + ".new";
        RejectLinks(temporary);
        if (File.Exists(temporary)) File.Delete(temporary);
        using (var stream = create?.Invoke(temporary) ?? new FileStream(temporary, FileMode.CreateNew, FileAccess.Write, FileShare.None))
        {
            JsonSerializer.Serialize(stream, value);
            stream.Flush(true);
        }
        File.Move(temporary, path, true);
    }

    public static UpdateSnapshot Capture(string root, string backup, Func<string, bool, string> security,
        Action<string>? createDirectory = null, Func<string, FileStream>? createFile = null)
    {
        RejectLinks(root);
        RejectLinks(backup);
        Directory.CreateDirectory(backup);
        var snapshot = new UpdateSnapshot();
        void CopyDirectory(string directory)
        {
            foreach (var path in Directory.EnumerateFileSystemEntries(directory))
            {
                RejectLinks(path);
                var relative = Path.GetRelativePath(root, path);
                var attributes = File.GetAttributes(path);
                var isDirectory = (attributes & FileAttributes.Directory) != 0;
                var destination = SafePath(backup, relative);
                var descriptor = security(path, isDirectory);
                if (isDirectory)
                {
                    if (createDirectory != null) createDirectory(destination); else Directory.CreateDirectory(destination);
                    CopyDirectory(path);
                }
                else
                {
                    using var input = File.OpenRead(path);
                    using var output = createFile?.Invoke(destination) ?? new FileStream(destination, FileMode.CreateNew, FileAccess.Write, FileShare.None);
                    input.CopyTo(output);
                    output.Flush(true);
                }
                var hash = isDirectory ? "" : Hash(destination);
                if (!isDirectory && Hash(path) != hash) throw new IOException("An installed file changed during backup.");
                snapshot.Files.Add(new UpdateFile(relative, isDirectory, (int)attributes,
                    File.GetLastWriteTimeUtc(path).Ticks, hash, descriptor));
            }
        }
        CopyDirectory(root);
        return snapshot;
    }

    public void Verify(string root, string backup)
    {
        foreach (var item in Files)
        {
            SafePath(root, item.Name);
            var source = SafePath(backup, item.Name);
            if (item.Directory ? !Directory.Exists(source) : !File.Exists(source) || Hash(source) != item.Hash)
                throw new IOException("The update backup is missing or damaged: " + item.Name);
        }
    }

    public void Restore(string root, string backup, Action<string, bool, string> restoreSecurity,
        Func<string, FileStream>? createFile = null)
    {
        Verify(root, backup);
        var names = Files.Select(f => f.Name).ToHashSet(StringComparer.OrdinalIgnoreCase);
        void RemoveNew(string directory)
        {
            foreach (var path in Directory.EnumerateFileSystemEntries(directory))
            {
                RejectLinks(path);
                if (Directory.Exists(path)) RemoveNew(path);
                if (!names.Contains(Path.GetRelativePath(root, path)))
                {
                    File.SetAttributes(path, FileAttributes.Normal);
                    if (Directory.Exists(path)) Directory.Delete(path); else File.Delete(path);
                }
            }
        }
        RejectLinks(root);
        RemoveNew(root);
        foreach (var item in Files.Where(f => f.Directory).OrderBy(f => f.Name.Length))
            Directory.CreateDirectory(SafePath(root, item.Name));
        foreach (var item in Files.Where(f => !f.Directory))
        {
            var target = SafePath(root, item.Name);
            if (File.Exists(target) && Hash(target) == item.Hash) continue;
            Replace(SafePath(backup, item.Name), target, createFile,
                path => restoreSecurity(path, false, item.Security));
        }
        foreach (var item in Files.OrderByDescending(f => f.Name.Length))
        {
            var path = SafePath(root, item.Name);
            File.SetLastWriteTimeUtc(path, new DateTime(item.Written, DateTimeKind.Utc));
            File.SetAttributes(path, (FileAttributes)item.Attributes);
            restoreSecurity(path, item.Directory, item.Security);
        }
    }

    public static void Replace(string source, string destination, Func<string, FileStream>? createFile = null,
        Action<string>? prepare = null)
    {
        RejectLinks(source);
        RejectLinks(destination);
        Directory.CreateDirectory(Path.GetDirectoryName(destination)!);
        var temporary = destination + ".bbupdate-" + Guid.NewGuid().ToString("N");
        RejectLinks(temporary);
        try
        {
            using (var input = File.OpenRead(source))
            using (var output = createFile?.Invoke(temporary) ?? new FileStream(temporary, FileMode.CreateNew, FileAccess.Write, FileShare.None))
            {
                input.CopyTo(output);
                output.Flush(true);
            }
            prepare?.Invoke(temporary);
            if (File.Exists(destination)) File.SetAttributes(destination, FileAttributes.Normal);
            File.Move(temporary, destination, true);
        }
        finally { if (File.Exists(temporary)) File.Delete(temporary); }
    }
}
