using System.IO;
using System.IO.Pipes;

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

public class WhitelistInfo
{
    public string Name { get; set; } = "";
    public int EntryCount { get; set; }
    public bool IsSelected { get; set; }
}

public class ServerStatus
{
    public bool IsRunning { get; set; }
    public TimeSpan Uptime { get; set; }
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

    private string SendCommand(string command)
    {
        if (_pipe == null || !_isConnected)
        {
            _lastError = "Not connected";
            return "";
        }

        try
        {
            var writer = new StreamWriter(_pipe!) { AutoFlush = true };
            var reader = new StreamReader(_pipe!);

            writer.WriteLine(command);

            var response = reader.ReadLine();
            return response?.Trim() ?? "";
        }
        catch (Exception ex)
        {
            _lastError = ex.Message;
            _isConnected = false;
            return "";
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
                var result = SendCommand("GET_SERVER_STATUS");
                status.IsRunning = result == "OK" || result.StartsWith("OK");
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
                var writer = new StreamWriter(_pipe!) { AutoFlush = true };
                var reader = new StreamReader(_pipe!);

                writer.WriteLine("GET_CLIENTS");

                string? line;
                while ((line = reader.ReadLine()) != null)
                {
                    if (string.IsNullOrWhiteSpace(line)) break;
                    if (line == "OK")
                    {
                        while ((line = reader.ReadLine()) != null)
                        {
                            if (string.IsNullOrWhiteSpace(line)) break;
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
                        break;
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
                var writer = new StreamWriter(_pipe!) { AutoFlush = true };
                var reader = new StreamReader(_pipe!);

                writer.WriteLine("GET_PENDING");

                string? line;
                while ((line = reader.ReadLine()) != null)
                {
                    if (string.IsNullOrWhiteSpace(line)) break;
                    if (line == "OK")
                    {
                        while ((line = reader.ReadLine()) != null)
                        {
                            if (string.IsNullOrWhiteSpace(line)) break;
                            var parts = line.Split(':');
                            if (parts.Length >= 2)
                            {
                                pending.Add(new PendingRegistration
                                {
                                    Name = parts[0],
                                    Address = parts[1]
                                });
                            }
                        }
                        break;
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
        return await Task.Run(() =>
        {
            _connectionLock.Wait();
            try
            {
                if (!ConnectAsync().Result) return false;

                try
                {
                    var result = SendCommand($"APPROVE:{name}");
                    Disconnect();
                    return result == "OK";
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
        });
    }

    public async Task<bool> RejectUserAsync(string name)
    {
        return await Task.Run(() =>
        {
            _connectionLock.Wait();
            try
            {
                if (!ConnectAsync().Result) return false;

                try
                {
                    var result = SendCommand($"REJECT:{name}");
                    Disconnect();
                    return result == "OK";
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
        });
    }

    public async Task<bool> DisconnectUserAsync(string name)
    {
        return await Task.Run(() =>
        {
            _connectionLock.Wait();
            try
            {
                if (!ConnectAsync().Result) return false;

                try
                {
                    var result = SendCommand($"DISCONNECT:{name}");
                    Disconnect();
                    return result == "OK";
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
        });
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
                var writer = new StreamWriter(_pipe!) { AutoFlush = true };
                var reader = new StreamReader(_pipe!);

                writer.WriteLine("GET_WHITELISTS");

                string? line;
                while ((line = reader.ReadLine()) != null)
                {
                    if (string.IsNullOrWhiteSpace(line)) break;
                    if (line == "OK")
                    {
                        while ((line = reader.ReadLine()) != null)
                        {
                            if (string.IsNullOrWhiteSpace(line)) break;
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
                        break;
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
                var writer = new StreamWriter(_pipe!) { AutoFlush = true };
                var reader = new StreamReader(_pipe!);

                writer.WriteLine($"GET_WHITELIST:{whitelistName}");

                string? line;
                while ((line = reader.ReadLine()) != null)
                {
                    if (string.IsNullOrWhiteSpace(line)) break;
                    if (line == "OK")
                    {
                        while ((line = reader.ReadLine()) != null)
                        {
                            if (string.IsNullOrWhiteSpace(line)) break;
                            entries.Add(line);
                        }
                        break;
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

        return entries;
    }

    public async Task<bool> AddWhitelistEntryAsync(string whitelistName, string entry)
    {
        return await Task.Run(() =>
        {
            _connectionLock.Wait();
            try
            {
                if (!ConnectAsync().Result) return false;

                try
                {
                    var result = SendCommand($"ADD_WHITELIST_ENTRY:{whitelistName}:{entry}");
                    Disconnect();
                    return result == "OK";
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
        });
    }

    public async Task<bool> DeleteWhitelistEntryAsync(string whitelistName, string entry)
    {
        return await Task.Run(() =>
        {
            _connectionLock.Wait();
            try
            {
                if (!ConnectAsync().Result) return false;

                try
                {
                    var result = SendCommand($"DELETE_WHITELIST_ENTRY:{whitelistName}:{entry}");
                    Disconnect();
                    return result == "OK";
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
        });
    }

    public async Task<bool> CreateWhitelistAsync(string name)
    {
        return await Task.Run(() =>
        {
            _connectionLock.Wait();
            try
            {
                if (!ConnectAsync().Result) return false;

                try
                {
                    var result = SendCommand($"CREATE_WHITELIST:{name}");
                    Disconnect();
                    return result == "OK";
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
        });
    }

    public async Task<bool> DeleteWhitelistAsync(string name)
    {
        return await Task.Run(() =>
        {
            _connectionLock.Wait();
            try
            {
                if (!ConnectAsync().Result) return false;

                try
                {
                    var result = SendCommand($"DELETE_WHITELIST:{name}");
                    Disconnect();
                    return result == "OK";
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
        });
    }

    public async Task<bool> SetActiveWhitelistAsync(string name)
    {
        return await Task.Run(() =>
        {
            _connectionLock.Wait();
            try
            {
                if (!ConnectAsync().Result) return false;

                try
                {
                    var result = SendCommand($"SET_ACTIVE_WHITELIST:{name}");
                    Disconnect();
                    return result == "OK";
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
        });
    }

    public async Task<bool> SaveWhitelistAsync(string name, List<string> entries)
    {
        return await Task.Run(() =>
        {
            _connectionLock.Wait();
            try
            {
                if (!ConnectAsync().Result) return false;

                try
                {
                    var writer = new StreamWriter(_pipe!) { AutoFlush = true };
                    var reader = new StreamReader(_pipe!);

                    writer.WriteLine($"SAVE_WHITELIST:{name}");
                    foreach (var entry in entries)
                    {
                        if (!string.IsNullOrWhiteSpace(entry))
                        {
                            writer.WriteLine(entry);
                        }
                    }
                    writer.WriteLine();

                    var response = reader.ReadLine();
                    Disconnect();
                    return response == "OK";
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
        });
    }

    public async Task<bool> RenameWhitelistAsync(string oldName, string newName)
    {
        return await Task.Run(() =>
        {
            _connectionLock.Wait();
            try
            {
                if (!ConnectAsync().Result) return false;

                try
                {
                    var result = SendCommand($"RENAME_WHITELIST:{oldName}:{newName}");
                    Disconnect();
                    return result == "OK";
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
        });
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