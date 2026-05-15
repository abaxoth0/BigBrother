using System.IO;
using System.IO.Pipes;
using System.Text;

namespace frontend.Services;

public class ClientStatus
{
    public string ClientName { get; set; } = "";
    public string IpAddress { get; set; } = "";
    public string DaemonStatus { get; set; } = "NOT_RUNNING";
    public string ClientBackendStatus { get; set; } = "NOT_RUNNING";
    public string ServerRunning { get; set; } = "NOT_RUNNING";
    public string ServerSessionActive { get; set; } = "NOT_CONNECTED";
    public int ClientPid { get; set; }
    public bool IsConnected => ClientBackendStatus == "RUNNING";
    public bool IsServerRunning => ServerRunning == "RUNNING";
    public bool IsServerSessionActive => ServerSessionActive == "CONNECTED";
    public uint WhitelistRevision { get; set; }
}

public class IpcService : IDisposable
{
    private const string PipeName = "BigBrother.Client.Backend";
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

            // Write TLV request: command\n<len>\n<arg>\n...\n(empty line)
            // Use Write() with explicit \n to avoid \r\n on Windows
            writer.Write(cmd + "\n");
            foreach (var arg in args)
            {
                writer.Write(Encoding.UTF8.GetByteCount(arg) + "\n");
                writer.Write(arg + "\n");
            }
            writer.Write("\n"); // empty line terminates request

            // Read response: status\n[TLV data...\n](empty line)
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
                var (responseStatus, data) = SendCommand("GET_STATUS");
                if (responseStatus == "OK" && data.Count >= 4)
                {
                    status.ClientName = data[0];
                    status.IpAddress = data[1];
                    status.DaemonStatus = data[2] == "running" ? "RUNNING" : "NOT_RUNNING";
                    status.ClientBackendStatus = data[3] == "running" ? "RUNNING" : "NOT_RUNNING";
                    if (data.Count > 4 && uint.TryParse(data[4], out uint revision))
                    {
                        status.WhitelistRevision = revision;
                    }
                    if (data.Count > 5 && uint.TryParse(data[5], out uint pid))
                    {
                        status.ClientPid = (int)pid;
                    }
                    if (data.Count > 6)
                    {
                        status.ServerRunning = data[6] == "running" ? "RUNNING" : "NOT_RUNNING";
                    }
                    if (data.Count > 7)
                    {
                        status.ServerSessionActive = data[7] == "connected" ? "CONNECTED" : "NOT_CONNECTED";
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
                var (status, data) = SendCommand("GET_WHITELIST");
                if (status == "OK")
                {
                    whitelist.AddRange(data);
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
        return await Task.Run(() =>
        {
            _connectionLock.Wait();
            try
            {
                if (!ConnectAsync().Result) return false;
                var (status, _) = SendCommand("RESTART_CLIENT");
                Disconnect();
                return status == "OK";
            }
            finally
            {
                _connectionLock.Release();
            }
        });
    }

    public async Task<bool> PingAsync()
    {
        return await Task.Run(() =>
        {
            _connectionLock.Wait();
            try
            {
                if (!ConnectAsync().Result) return false;
                var (status, _) = SendCommand("PING");
                Disconnect();
                return status == "OK";
            }
            finally
            {
                _connectionLock.Release();
            }
        });
    }

    public async Task<bool> SetFallbackWhitelistEnabledAsync(bool enabled)
    {
        await _connectionLock.WaitAsync();
        try
        {
            if (!await ConnectAsync()) return false;
            try
            {
                var (status, _) = SendCommand("SET_FALLBACK_WHITELIST", enabled ? "1" : "0");
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

    public async Task<bool> GetFallbackWhitelistEnabledAsync()
    {
        await _connectionLock.WaitAsync();
        try
        {
            if (!await ConnectAsync()) return false;
            try
            {
                var (status, data) = SendCommand("GET_FALLBACK_WHITELIST");
                return status == "OK" && data.Count > 0 && data[0] == "1";
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

                        var (status, data) = SendCommand("GET_LOG_PATH");
                        Disconnect();

                        if (status == "OK" && data.Count >= 2)
                        {
                            return (data[0], data[1]);
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
