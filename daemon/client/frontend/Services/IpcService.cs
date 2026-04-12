using System.IO;
using System.IO.Pipes;

namespace frontend.Services;

public class ClientStatus
{
    public string ClientName { get; set; } = "";
    public string IpAddress { get; set; } = "";
    public string DaemonStatus { get; set; } = "NOT_RUNNING";
    public string ClientBackendStatus { get; set; } = "NOT_RUNNING";
    public int ClientPid { get; set; }
    public bool IsConnected => ClientBackendStatus == "RUNNING";
    public uint WhitelistRevision { get; set; }
}

public class IpcService : IDisposable
{
    private const string PipeName = "BigBrother Client";
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
            // Ensure any previous pipe is fully disposed
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
        finally
        {
            // Connection state is managed by caller via lock
        }
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

    private string SendRawCommand(string command)
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

    public async Task<ClientStatus> GetStatusAsync()
    {
        var status = new ClientStatus();

        await _connectionLock.WaitAsync();
        try
        {
            if (!await ConnectAsync())
            {
                return status;
            }

            try
            {
                await Task.Delay(50);
                
                var writer = new StreamWriter(_pipe!) { AutoFlush = true };
                var reader = new StreamReader(_pipe!);

                writer.WriteLine("GET_STATUS");

                var lines = new List<string>();
                for (int i = 0; i < 5; i++)
                {
                    var line = reader.ReadLine();
                    if (line == null) break;
                    lines.Add(line);
                }
                
                if (lines.Count >= 5 && lines[0] == "STATUS")
                {
                    if (uint.TryParse(lines[3], out var revision))
                    {
                        status.WhitelistRevision = revision;
                    }
                    
                    if (lines[4] == "running")
                    {
                        status.ClientBackendStatus = "RUNNING";
                        status.ClientName = "BigBrother Client";
                        status.IpAddress = "127.0.0.1";
                        status.DaemonStatus = "RUNNING";
                        status.ClientPid = Environment.ProcessId;
                    }
                }
                else if (lines.Count > 0 && lines[0].StartsWith("STATUS:"))
                {
                    var firstLine = lines[0];
                    var parts = firstLine.Split(':');
                    if (parts.Length >= 6)
                    {
                        status.ClientName = parts[1];
                        status.IpAddress = parts[2];
                        status.DaemonStatus = parts[3];
                        if (ulong.TryParse(parts[4], out var pid))
                        {
                            status.ClientPid = (int)pid;
                        }
                        if (uint.TryParse(parts[5], out var revision))
                        {
                            status.WhitelistRevision = revision;
                        }
                        status.ClientBackendStatus = parts[3] == "RUNNING" ? "RUNNING" : "NOT_RUNNING";
                    }
                    else if (parts.Length >= 5)
                    {
                        status.ClientName = parts[1];
                        status.IpAddress = parts[2];
                        status.DaemonStatus = parts[3];
                        if (ulong.TryParse(parts[4], out var pid))
                        {
                            status.ClientPid = (int)pid;
                        }
                        status.WhitelistRevision = 1;
                        status.ClientBackendStatus = parts[3] == "RUNNING" ? "RUNNING" : "NOT_RUNNING";
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

    public async Task<List<string>> GetWhitelistAsync()
    {
        var whitelist = new List<string>();

        await _connectionLock.WaitAsync();
        try
        {
            if (!await ConnectAsync())
            {
                return whitelist;
            }

            try
            {
                var writer = new StreamWriter(_pipe!) { AutoFlush = true };
                var reader = new StreamReader(_pipe!);

                writer.WriteLine("GET_WHITELIST");

                var firstLine = reader.ReadLine();
                if (firstLine == "WHITELIST")
                {
                    string? line;
                    while ((line = reader.ReadLine()) != null)
                    {
                        if (string.IsNullOrWhiteSpace(line)) break;
                        if (line == "WHITELIST") continue;
                        whitelist.Add(line);
                    }
                }
                else if (!string.IsNullOrWhiteSpace(firstLine))
                {
                    whitelist.Add(firstLine);
                    string? line;
                    while ((line = reader.ReadLine()) != null)
                    {
                        if (string.IsNullOrWhiteSpace(line)) break;
                        whitelist.Add(line);
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

        return whitelist;
    }

    public async Task<bool> RestartClientAsync()
    {
        await _connectionLock.WaitAsync();
        try
        {
            if (!await ConnectAsync())
            {
                return false;
            }

            try
            {
                var writer = new StreamWriter(_pipe!) { AutoFlush = true };
                var reader = new StreamReader(_pipe!);

                writer.WriteLine("RESTART_CLIENT");
                var response = reader.ReadLine();

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
    }

    public async Task<bool> PingAsync()
    {
        await _connectionLock.WaitAsync();
        try
        {
            if (!await ConnectAsync())
            {
                return false;
            }

            try
            {
                var writer = new StreamWriter(_pipe!) { AutoFlush = true };
                var reader = new StreamReader(_pipe!);

                writer.WriteLine("PING");
                var response = reader.ReadLine();

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
    }

    public async Task<(string clientLog, string firewallLog)> GetLogPathAsync()
    {
        await _connectionLock.WaitAsync();
        try
        {
            for (int attempt = 0; attempt < MaxRetries; attempt++)
            {
                if (await ConnectAsync(3000))
                {
                    try
                    {
                        await Task.Delay(50);
                        
                        var writer = new StreamWriter(_pipe!) { AutoFlush = true };
                        var reader = new StreamReader(_pipe!);

                        writer.WriteLine("GET_LOG_PATH");
                        var response = reader.ReadLine();
                        
                        Disconnect();
                        
                        if (response != null && response.StartsWith("LOG_PATH:"))
                        {
                            var paths = response.Substring(9).Split('|');
                            if (paths.Length >= 2)
                            {
                                return (paths[0], paths[1]);
                            }
                        }
                        return ("", "");
                    }
                    catch
                    {
                        Disconnect();
                    }
                }
                
                if (attempt < MaxRetries - 1) await Task.Delay(500);
            }
            
            return ("", "");
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
