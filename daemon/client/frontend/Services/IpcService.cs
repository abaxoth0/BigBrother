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
    public bool FiltrationEnabled { get; set; } = true;
}

public class IpcService : IDisposable
{
    private const string PipeName = "BigBrother.Client.Backend";
    private const int MaxRetries = 3;
    private const int ReadTimeoutMs = 10000;
    private NamedPipeClientStream? _pipe;
    private bool _isConnected;
    private string _lastError = "";
    private readonly SemaphoreSlim _connectionLock = new(1, 1);
    private CancellationTokenSource? _eventSubCts;

    public event Action? StateChanged;

    public bool IsConnected => _isConnected;
    public string LastError => _lastError;

    public void StartEventSubscription()
    {
        if (_eventSubCts != null) return;
        _eventSubCts = new CancellationTokenSource();
        _ = SubscribeLoopAsync(_eventSubCts.Token);
    }

    public void StopEventSubscription()
    {
        if (_eventSubCts != null)
        {
            _eventSubCts.Cancel();
            _eventSubCts.Dispose();
            _eventSubCts = null;
        }
    }

    private async Task SubscribeLoopAsync(CancellationToken ct)
    {
        while (!ct.IsCancellationRequested)
        {
            try
            {
                using var pipe = new NamedPipeClientStream(".", PipeName, PipeDirection.InOut);
                // Non-blocking probe: fails instantly when the daemon is not running
                // (a timed ConnectAsync busy-waits for the full timeout, burning CPU).
                await pipe.ConnectAsync(0, ct);
                var writer = new StreamWriter(pipe) { AutoFlush = true };
                var reader = new StreamReader(pipe);

                // SUBSCRIBE\n\n (no username needed)
                await writer.WriteAsync("SUBSCRIBE\n\n").ConfigureAwait(false);
                await writer.FlushAsync().ConfigureAwait(false);

                var status = await reader.ReadLineAsync(ct).ConfigureAwait(false);
                if (status != "OK")
                {
                    await Task.Delay(1000, ct).ConfigureAwait(false);
                    continue;
                }
                // consume empty line terminator
                await reader.ReadLineAsync(ct).ConfigureAwait(false);

                // Subscribed — signal UI to refresh once (covers reconnects)
                StateChanged?.Invoke();

                while (!ct.IsCancellationRequested)
                {
                    var line = await reader.ReadLineAsync(ct).ConfigureAwait(false);
                    if (line == null) break; // pipe closed
                    if (line == "EVENT")
                    {
                        StateChanged?.Invoke();
                        // Skip remaining event TLV lines
                        string? evLine;
                        while ((evLine = await reader.ReadLineAsync(ct).ConfigureAwait(false)) != null && evLine.Length > 0)
                        {
                            // consume until empty line
                        }
                    }
                }
            }
            catch (OperationCanceledException)
            {
                return;
            }
            catch (Exception)
            {
                // pipe unavailable — retry after delay
            }

            if (!ct.IsCancellationRequested)
            {
                try { await Task.Delay(1000, ct).ConfigureAwait(false); } catch (OperationCanceledException) { return; }
            }
        }
    }

    public async Task<bool> ConnectAsync(int timeoutMs = 3000)
    {
        // IMPORTANT: use a non-blocking probe (ConnectAsync(0)). When the pipe
        // server is absent, a timed ConnectAsync busy-waits (SpinWait) for the
        // full timeout, burning CPU on every retry while the daemon is down.
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
                    await _pipe.ConnectAsync(0);
                    _isConnected = true;
                    _lastError = "";
                    return true;
                }
                catch (TimeoutException)
                {
                    // Daemon not running — fail fast instead of busy-waiting.
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

    private async Task<(string status, List<string> data)> SendCommandAsync(string cmd, string[] args, CancellationToken ct = default)
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

            // Write TLV request
            await writer.WriteAsync(cmd + "\n").ConfigureAwait(false);
            foreach (var arg in args)
            {
                await writer.WriteAsync(Encoding.UTF8.GetByteCount(arg) + "\n").ConfigureAwait(false);
                await writer.WriteAsync(arg + "\n").ConfigureAwait(false);
            }
            await writer.WriteAsync("\n").ConfigureAwait(false);
            await writer.FlushAsync().ConfigureAwait(false);

            // Read response with timeout
            var status = await reader.ReadLineAsync(ct).ConfigureAwait(false);
            if (string.IsNullOrEmpty(status)) return ("", new List<string>());

            var data = new List<string>();
            if (status == "OK")
            {
                while (true)
                {
                    var lenLine = await reader.ReadLineAsync(ct).ConfigureAwait(false);
                    if (string.IsNullOrEmpty(lenLine)) break;

                    if (!int.TryParse(lenLine, out int len) || len < 0)
                        break;

                    var value = await reader.ReadLineAsync(ct).ConfigureAwait(false);
                    if (value == null) break;
                    if (Encoding.UTF8.GetByteCount(value) != len) break;

                    data.Add(value);
                }
            }
            else if (status == "ERROR")
            {
                var lenLine = await reader.ReadLineAsync(ct).ConfigureAwait(false);
                if (!string.IsNullOrEmpty(lenLine) && int.TryParse(lenLine, out int len))
                {
                    var errorMsg = await reader.ReadLineAsync(ct).ConfigureAwait(false);
                    _lastError = errorMsg ?? "Unknown error";
                }
            }

            return (status, data);
        }
        catch (OperationCanceledException)
        {
            _lastError = "Command timed out";
            _isConnected = false;
            return ("", new List<string>());
        }
        catch (Exception ex)
        {
            _lastError = ex.Message;
            _isConnected = false;
            return ("", new List<string>());
        }
    }

    private async Task<(string status, List<string> data)> SendCommandWithTimeoutAsync(string cmd, int timeoutMs, params string[] args)
    {
        using var cts = new CancellationTokenSource(timeoutMs);
        try
        {
            return await SendCommandAsync(cmd, args, cts.Token).ConfigureAwait(false);
        }
        catch (OperationCanceledException)
        {
            _lastError = "Command timed out";
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
                var (responseStatus, data) = await SendCommandWithTimeoutAsync("GET_STATUS", ReadTimeoutMs);
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
                    if (data.Count > 8)
                    {
                        status.FiltrationEnabled = data[8] == "1";
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
                var (status, data) = await SendCommandWithTimeoutAsync("GET_WHITELIST", ReadTimeoutMs);
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
        await _connectionLock.WaitAsync();
        try
        {
            if (!await ConnectAsync()) return false;
            var (status, _) = await SendCommandWithTimeoutAsync("RESTART_CLIENT", 3000);
            Disconnect();
            return status == "OK";
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
            if (!await ConnectAsync()) return false;
            var (status, _) = await SendCommandWithTimeoutAsync("PING", 5000);
            Disconnect();
            return status == "OK";
        }
        finally
        {
            _connectionLock.Release();
        }
    }

    public async Task<bool> SetFallbackWhitelistEnabledAsync(bool enabled)
    {
        await _connectionLock.WaitAsync();
        try
        {
            if (!await ConnectAsync()) return false;
            try
            {
                var (status, _) = await SendCommandWithTimeoutAsync("SET_FALLBACK_WHITELIST", ReadTimeoutMs, enabled ? "1" : "0");
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
                var (status, data) = await SendCommandWithTimeoutAsync("GET_FALLBACK_WHITELIST", ReadTimeoutMs);
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

    public async Task<bool> SetServerAddressAsync(string address)
    {
        await _connectionLock.WaitAsync();
        try
        {
            if (!await ConnectAsync()) return false;
            try
            {
                var (status, _) = await SendCommandWithTimeoutAsync("SET_SERVER_ADDR", ReadTimeoutMs, address);
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

    public async Task<string> GetServerAddressAsync()
    {
        await _connectionLock.WaitAsync();
        try
        {
            if (!await ConnectAsync()) return "";
            try
            {
                var (status, data) = await SendCommandWithTimeoutAsync("GET_SERVER_ADDR", ReadTimeoutMs);
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

    public async Task<bool> SetUsernameAsync(string name)
    {
        await _connectionLock.WaitAsync();
        try
        {
            if (!await ConnectAsync()) return false;
            try
            {
                var (status, _) = await SendCommandWithTimeoutAsync("SET_USERNAME", ReadTimeoutMs, name);
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

    public async Task<string> GetUsernameAsync()
    {
        await _connectionLock.WaitAsync();
        try
        {
            if (!await ConnectAsync()) return "";
            try
            {
                var (status, data) = await SendCommandWithTimeoutAsync("GET_USERNAME", ReadTimeoutMs);
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

    public async Task<List<string>> DiscoverServersAsync(int timeoutMs = 2000)
    {
        await _connectionLock.WaitAsync();
        try
        {
            if (!await ConnectAsync()) return new List<string>();
            try
            {
                var (status, data) = await SendCommandWithTimeoutAsync("DISCOVER_SERVERS", ReadTimeoutMs, timeoutMs.ToString());
                return status == "OK" ? data : new List<string>();
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

    public async Task<bool> SetServerNameAsync(string name)
    {
        await _connectionLock.WaitAsync();
        try
        {
            if (!await ConnectAsync()) return false;
            try
            {
                var (status, _) = await SendCommandWithTimeoutAsync("SET_SERVER_NAME", ReadTimeoutMs, name);
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

    public async Task<string> GetServerNameAsync()
    {
        await _connectionLock.WaitAsync();
        try
        {
            if (!await ConnectAsync()) return "";
            try
            {
                var (status, data) = await SendCommandWithTimeoutAsync("GET_SERVER_NAME", ReadTimeoutMs);
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

    public async Task<bool> SetServerPortAsync(string port)
    {
        await _connectionLock.WaitAsync();
        try
        {
            if (!await ConnectAsync()) return false;
            try
            {
                var (status, _) = await SendCommandWithTimeoutAsync("SET_SERVER_PORT", ReadTimeoutMs, port);
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

    public async Task<string> GetServerPortAsync()
    {
        await _connectionLock.WaitAsync();
        try
        {
            if (!await ConnectAsync()) return "";
            try
            {
                var (status, data) = await SendCommandWithTimeoutAsync("GET_SERVER_PORT", ReadTimeoutMs);
                return status == "OK" && data.Count > 0 ? data[0] : "1984";
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

    public async Task<bool> SetDiscoveryEnabledAsync(bool enabled)
    {
        await _connectionLock.WaitAsync();
        try
        {
            if (!await ConnectAsync()) return false;
            try
            {
                var (status, _) = await SendCommandWithTimeoutAsync("SET_DISCOVERY_ENABLED", ReadTimeoutMs, enabled ? "1" : "0");
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

    public async Task<bool> GetDiscoveryEnabledAsync()
    {
        await _connectionLock.WaitAsync();
        try
        {
            if (!await ConnectAsync()) return true;
            try
            {
                var (status, data) = await SendCommandWithTimeoutAsync("GET_DISCOVERY_ENABLED", ReadTimeoutMs);
                return status != "OK" || data.Count <= 0 || data[0] == "1";
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

    public async Task<bool> SetNetworkAutoAsync(bool auto)
    {
        await _connectionLock.WaitAsync();
        try
        {
            if (!await ConnectAsync()) return false;
            try
            {
                var (status, _) = await SendCommandWithTimeoutAsync("SET_NETWORK_AUTO", ReadTimeoutMs, auto ? "1" : "0");
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

    public async Task<bool> GetNetworkAutoAsync()
    {
        await _connectionLock.WaitAsync();
        try
        {
            if (!await ConnectAsync()) return true;
            try
            {
                var (status, data) = await SendCommandWithTimeoutAsync("GET_NETWORK_AUTO", ReadTimeoutMs);
                return status != "OK" || data.Count <= 0 || data[0] == "1";
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

    public async Task<bool> SetNetworkGatewayAsync(string gateway)
    {
        await _connectionLock.WaitAsync();
        try
        {
            if (!await ConnectAsync()) return false;
            try
            {
                var (status, _) = await SendCommandWithTimeoutAsync("SET_NETWORK_GATEWAY", ReadTimeoutMs, gateway);
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

    public async Task<string> GetNetworkGatewayAsync()
    {
        await _connectionLock.WaitAsync();
        try
        {
            if (!await ConnectAsync()) return "";
            try
            {
                var (status, data) = await SendCommandWithTimeoutAsync("GET_NETWORK_GATEWAY", ReadTimeoutMs);
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

    public async Task<bool> SetNetworkMaskAsync(string mask)
    {
        await _connectionLock.WaitAsync();
        try
        {
            if (!await ConnectAsync()) return false;
            try
            {
                var (status, _) = await SendCommandWithTimeoutAsync("SET_NETWORK_MASK", ReadTimeoutMs, mask);
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

    public async Task<string> GetNetworkMaskAsync()
    {
        await _connectionLock.WaitAsync();
        try
        {
            if (!await ConnectAsync()) return "";
            try
            {
                var (status, data) = await SendCommandWithTimeoutAsync("GET_NETWORK_MASK", ReadTimeoutMs);
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

    public async Task<bool> RegisterAsync(string username)
    {
        await _connectionLock.WaitAsync();
        try
        {
            if (!await ConnectAsync()) return false;
            try
            {
                var (status, _) = await SendCommandWithTimeoutAsync("REGISTER", ReadTimeoutMs, username);
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

    public async Task<bool> ConnectToServerAsync(string username)
    {
        await _connectionLock.WaitAsync();
        try
        {
            if (!await ConnectAsync()) return false;
            try
            {
                var (status, _) = await SendCommandWithTimeoutAsync("CONNECT", ReadTimeoutMs);
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

    public async Task<bool> DisconnectFromServerAsync(string username)
    {
        await _connectionLock.WaitAsync();
        try
        {
            if (!await ConnectAsync()) return false;
            try
            {
                var (status, _) = await SendCommandWithTimeoutAsync("DISCONNECT", ReadTimeoutMs);
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
                        var (status, data) = await SendCommandWithTimeoutAsync("GET_LOG_PATH", ReadTimeoutMs);
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

    public async Task<bool> GetFiltrationEnabledAsync()
    {
        await _connectionLock.WaitAsync();
        try
        {
            if (!await ConnectAsync()) return true;
            try
            {
                var (status, data) = await SendCommandWithTimeoutAsync("GET_FILTRATION", ReadTimeoutMs);
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

    public async Task<bool> SetFiltrationEnabledAsync(bool enabled)
    {
        await _connectionLock.WaitAsync();
        try
        {
            if (!await ConnectAsync()) return false;
            try
            {
                var (status, _) = await SendCommandWithTimeoutAsync("SET_FILTRATION", ReadTimeoutMs, enabled ? "1" : "0");
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

    public async Task<bool> GetFiltrationAutoDisableAsync()
    {
        await _connectionLock.WaitAsync();
        try
        {
            if (!await ConnectAsync()) return false;
            try
            {
                var (status, data) = await SendCommandWithTimeoutAsync("GET_FILTRATION_AUTO_DISABLE", ReadTimeoutMs);
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

    public async Task<bool> SetFiltrationAutoDisableAsync(bool enabled)
    {
        await _connectionLock.WaitAsync();
        try
        {
            if (!await ConnectAsync()) return false;
            try
            {
                var (status, _) = await SendCommandWithTimeoutAsync("SET_FILTRATION_AUTO_DISABLE", ReadTimeoutMs, enabled ? "1" : "0");
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

    public void Dispose()
    {
        StopEventSubscription();
        try
        {
            if (_pipe != null)
            {
                _pipe.Close();
                _pipe.Dispose();
            }
        }
        catch { }
        _pipe = null;
        _isConnected = false;
        _connectionLock.Dispose();
    }
}
