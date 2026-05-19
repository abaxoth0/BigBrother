using System.ComponentModel;
using System.IO;
using System.IO.Pipes;
using System.Linq;
using System.Runtime.CompilerServices;
using System.Text;

namespace frontend.Services;

public class ConnectedClient
{
    public string Name { get; set; } = "";
    public string Address { get; set; } = "";
    public string Status { get; set; } = "";
    public string Whitelist { get; set; } = "";
    public DateTime LastActivity { get; set; }
    public bool IsSelected { get; set; }
}

public class PendingRegistration
{
    public string Name { get; set; } = "";
    public string Address { get; set; } = "";
    public DateTime CreatedAt { get; set; }
}

public class WhitelistInfo : INotifyPropertyChanged
{
    public event PropertyChangedEventHandler? PropertyChanged;

    private int _entryCount;
    public string Name { get; set; } = "";
    public int EntryCount
    {
        get => _entryCount;
        set
        {
            if (_entryCount != value)
            {
                _entryCount = value;
                PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(nameof(EntryCount)));
            }
        }
    }
    public bool IsSelected { get; set; }
}

public class ServerStatus
{
    public bool IsRunning { get; set; }
    public string UptimeText { get; set; } = "";
    public int ConnectedClients { get; set; }
    public int PendingCount { get; set; }
}

public class IpcService : IDisposable
{
    private const string PipeName = "BigBrother.Server.Frontend";
    private const int MaxRetries = 3;
    private NamedPipeClientStream? _pipe;
    private bool _isConnected;
    private string _lastError = "";
    private readonly SemaphoreSlim _connectionLock = new(1, 1);

    public bool IsConnected => _isConnected;
    public string LastError => _lastError;

    public async Task<bool> ConnectAsync(int timeoutMs = 1000)
    {
        try
        {
            if (_pipe != null)
            {
                try { _pipe.Dispose(); } catch { }
                _pipe = null;
                _isConnected = false;
            }

            for (int attempt = 0; attempt < MaxRetries; attempt++)
            {
                try
                {
                    _pipe = new NamedPipeClientStream(".", PipeName, PipeDirection.InOut);
                    await _pipe.ConnectAsync(timeoutMs);
                    _isConnected = true;
                    _lastError = "";
                    return true;
                }
                catch (Exception)
                {
                    _isConnected = false;
                    if (_pipe != null)
                    {
                        try { _pipe.Dispose(); } catch { }
                        _pipe = null;
                    }

                    if (attempt < MaxRetries - 1)
                    {
                        await Task.Delay(50 * (attempt + 1));
                    }
                }
            }

            _lastError = "Failed to connect after " + MaxRetries + " attempts";
            return false;
        }
        finally { }
    }

    private void Disconnect()
    {
        try
        {
            if (_pipe != null)
            {
                _pipe.Flush();
                _pipe.Close();
                _pipe.Dispose();
            }
        }
        catch { }
        finally
        {
            _pipe = null;
            _isConnected = false;
        }
    }

    private (string status, List<string> data) SendCommand(string cmd, params string[] args)
    {
        if (_pipe == null || !_isConnected)
        {
            _lastError = "Not connected";
            return ("", new List<string>());
        }

        try
        {
            var writer = new StreamWriter(_pipe) { AutoFlush = true };
            var reader = new StreamReader(_pipe);

            // Write TLV request: command\n<byte_len>\n<arg>\n...\n (empty line terminates)
            // Use Write() with explicit \n to avoid \r\n on Windows
            // Length is UTF-8 byte count to match Go's len() (protocol.go)
            writer.Write(cmd + "\n");
            foreach (var arg in args)
            {
                writer.Write(Encoding.UTF8.GetByteCount(arg) + "\n");
                writer.Write(arg + "\n");
            }
            writer.Write("\n"); // empty line terminates request

            // Read response: status\n[TLV data...\n] (empty line terminates)
            var status = reader.ReadLine();
            if (string.IsNullOrEmpty(status)) return ("", new List<string>());

            var data = new List<string>();
            if (status == "OK")
            {
                // Read TLV data until empty line
                while (true)
                {
                    var lenLine = reader.ReadLine();
                    if (string.IsNullOrEmpty(lenLine)) break; // empty line terminates response

                    if (!int.TryParse(lenLine, out int len) || len < 0)
                    {
                        break; // invalid TLV
                    }

                    var value = reader.ReadLine();
                    if (value == null) break;
                    if (Encoding.UTF8.GetByteCount(value) != len) break; // length mismatch

                    data.Add(value);
                }
            }
            else if (status == "ERROR")
            {
                // Read error message as TLV
                var lenLine = reader.ReadLine();
                if (!string.IsNullOrEmpty(lenLine) && int.TryParse(lenLine, out int len))
                {
                    var errorMsg = reader.ReadLine();
                    _lastError = errorMsg ?? "Unknown error";
                }
            }

            return (status, data);
        }
        catch (Exception ex)
        {
            _lastError = ex.Message;
            _isConnected = false;
            return ("", new List<string>());
        }
    }

    public async Task<ServerStatus> GetServerStatusAsync()
    {
        var status = new ServerStatus { IsRunning = false };

        await _connectionLock.WaitAsync();
        try
        {
            if (!await ConnectAsync())
            {
                return status;
            }

            try
            {
                var (responseStatus, data) = SendCommand("GET_SERVER_STATUS");
                status.IsRunning = responseStatus == "OK";
                if (status.IsRunning && data.Count > 0)
                {
                    var parts = data[0].Split(':');
                    for (int i = 0; i < parts.Length - 1; i += 2)
                    {
                        switch (parts[i])
                        {
                            case "uptime":
                                status.UptimeText = parts[i + 1];
                                break;
                            case "clients":
                                if (int.TryParse(parts[i + 1], out var clients))
                                    status.ConnectedClients = clients;
                                break;
                            case "pending":
                                if (int.TryParse(parts[i + 1], out var pending))
                                    status.PendingCount = pending;
                                break;
                        }
                    }
                }
            }
            finally
            {
                Disconnect();
            }
        }
        finally
        {
            _connectionLock.Release();
        }

        return status;
    }

    public async Task<List<ConnectedClient>> GetClientsAsync()
    {
        var clients = new List<ConnectedClient>();

        await _connectionLock.WaitAsync();
        try
        {
            if (!await ConnectAsync())
            {
                return clients;
            }

            try
            {
                var (status, data) = SendCommand("GET_CLIENTS");
                if (status == "OK")
                {
                    foreach (var line in data)
                    {
                        var parts = line.Split(':');
                        if (parts.Length >= 2)
                        {
                            clients.Add(new ConnectedClient
                            {
                                Name = parts[0],
                                Address = parts[1],
                                Status = parts.Length > 2 ? parts[2] : "Active"
                            });
                        }
                    }
                }
            }
            finally
            {
                Disconnect();
            }
        }
        finally
        {
            _connectionLock.Release();
        }

        return clients;
    }

    public async Task<List<PendingRegistration>> GetPendingAsync()
    {
        var pending = new List<PendingRegistration>();

        await _connectionLock.WaitAsync();
        try
        {
            if (!await ConnectAsync())
            {
                return pending;
            }

            try
            {
                var (status, data) = SendCommand("GET_PENDING");
                if (status == "OK")
                {
                    foreach (var line in data)
                    {
                        var parts = line.Split(':');
                        if (parts.Length >= 2)
                        {
                            var reg = new PendingRegistration
                            {
                                Name = parts[0],
                                Address = parts[1]
                            };
                            if (parts.Length > 2 && long.TryParse(parts[2], out var unixSecs))
                            {
                                reg.CreatedAt = DateTimeOffset.FromUnixTimeSeconds(unixSecs).LocalDateTime;
                            }
                            pending.Add(reg);
                        }
                    }
                }
            }
            finally
            {
                Disconnect();
            }
        }
        finally
        {
            _connectionLock.Release();
        }

        return pending;
    }

    public async Task<bool> ApproveUserAsync(string name)
    {
        await _connectionLock.WaitAsync();
        try
        {
            if (!await ConnectAsync()) return false;
            try
            {
                var (status, _) = SendCommand("APPROVE", name);
                return status == "OK";
            }
            finally
            {
                Disconnect();
            }
        }
        finally
        {
            _connectionLock.Release();
        }
    }

    public async Task<bool> RejectUserAsync(string name)
    {
        await _connectionLock.WaitAsync();
        try
        {
            if (!await ConnectAsync()) return false;
            try
            {
                var (status, _) = SendCommand("REJECT", name);
                return status == "OK";
            }
            finally
            {
                Disconnect();
            }
        }
        finally
        {
            _connectionLock.Release();
        }
    }

    public async Task<bool> DisconnectUserAsync(string name)
    {
        await _connectionLock.WaitAsync();
        try
        {
            if (!await ConnectAsync()) return false;
            try
            {
                var (status, _) = SendCommand("DISCONNECT", name);
                return status == "OK";
            }
            finally
            {
                Disconnect();
            }
        }
        finally
        {
            _connectionLock.Release();
        }
    }

    public async Task<List<WhitelistInfo>> GetWhitelistsAsync()
    {
        var whitelists = new List<WhitelistInfo>();

        await _connectionLock.WaitAsync();
        try
        {
            if (!await ConnectAsync())
            {
                return whitelists;
            }

            try
            {
                var (status, data) = SendCommand("GET_WHITELISTS");
                if (status == "OK")
                {
                    foreach (var line in data)
                    {
                        var parts = line.Split(':');
                        if (parts.Length >= 1)
                        {
                            whitelists.Add(new WhitelistInfo
                            {
                                Name = parts[0],
                                EntryCount = parts.Length > 1 && int.TryParse(parts[1], out var count) ? count : 0
                            });
                        }
                    }
                }
            }
            finally
            {
                Disconnect();
            }
        }
        finally
        {
            _connectionLock.Release();
        }

        return whitelists;
    }

    public async Task<List<string>> GetWhitelistEntriesAsync(string whitelistName)
    {
        var entries = new List<string>();

        await _connectionLock.WaitAsync();
        try
        {
            if (!await ConnectAsync())
            {
                return entries;
            }

            try
            {
                var (status, data) = SendCommand("GET_WHITELIST", whitelistName);
                if (status == "OK")
                {
                    entries.AddRange(data);
                }
            }
            finally
            {
                Disconnect();
            }
        }
        finally
        {
            _connectionLock.Release();
        }

        return entries;
    }

    public async Task<bool> CreateWhitelistAsync(string name)
    {
        await _connectionLock.WaitAsync();
        try
        {
            if (!await ConnectAsync()) return false;
            try
            {
                var (status, _) = SendCommand("CREATE_WHITELIST", name);
                return status == "OK";
            }
            finally
            {
                Disconnect();
            }
        }
        finally
        {
            _connectionLock.Release();
        }
    }

    public async Task<bool> DeleteWhitelistAsync(string name)
    {
        await _connectionLock.WaitAsync();
        try
        {
            if (!await ConnectAsync()) return false;
            try
            {
                var (status, _) = SendCommand("DELETE_WHITELIST", name);
                return status == "OK";
            }
            finally
            {
                Disconnect();
            }
        }
        finally
        {
            _connectionLock.Release();
        }
    }

    public async Task<bool> SetActiveWhitelistAsync(string name)
    {
        await _connectionLock.WaitAsync();
        try
        {
            if (!await ConnectAsync()) return false;
            try
            {
                var (status, _) = SendCommand("SET_ACTIVE_WHITELIST", name);
                return status == "OK";
            }
            finally
            {
                Disconnect();
            }
        }
        finally
        {
            _connectionLock.Release();
        }
    }

    public async Task<bool> SaveWhitelistAsync(string name, List<string> entries)
    {
        var args = new List<string> { name };
        args.AddRange(entries.Where(e => !string.IsNullOrWhiteSpace(e)));
        await _connectionLock.WaitAsync();
        try
        {
            if (!await ConnectAsync()) return false;
            try
            {
                var (status, _) = SendCommand("SAVE_WHITELIST", args.ToArray());
                return status == "OK";
            }
            finally
            {
                Disconnect();
            }
        }
        finally
        {
            _connectionLock.Release();
        }
    }

    public async Task<bool> RenameWhitelistAsync(string oldName, string newName)
    {
        await _connectionLock.WaitAsync();
        try
        {
            if (!await ConnectAsync()) return false;
            try
            {
                var (status, _) = SendCommand("RENAME_WHITELIST", oldName, newName);
                return status == "OK";
            }
            finally
            {
                Disconnect();
            }
        }
        finally
        {
            _connectionLock.Release();
        }
    }

    public async Task<string> GetActiveWhitelistAsync()
    {
        await _connectionLock.WaitAsync();
        try
        {
            if (!await ConnectAsync()) return "";
            try
            {
                var (status, data) = SendCommand("GET_ACTIVE_WHITELIST");
                return status == "OK" && data.Count > 0 ? data[0] : "";
            }
            finally
            {
                Disconnect();
            }
        }
        finally
        {
            _connectionLock.Release();
        }
    }

    public void Dispose()
    {
        _connectionLock.Wait();
        try
        {
            Disconnect();
        }
        finally
        {
            _connectionLock.Release();
            _connectionLock.Dispose();
        }
    }
}
