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
}

public class IpcService : IDisposable
{
    private const string PipeName = "BigBrother Client";
    private NamedPipeClientStream? _pipe;
    private bool _isConnected;
    private string _lastError = "";

    public bool IsConnected => _isConnected;
    public string LastError => _lastError;

    public async Task<bool> ConnectAsync(int timeoutMs = 1000)
    {
        try
        {
            _pipe = new NamedPipeClientStream(".", PipeName, PipeDirection.InOut);
            await _pipe.ConnectAsync(timeoutMs);
            _isConnected = true;
            _lastError = "";
            return true;
        }
        catch (Exception ex)
        {
            _isConnected = false;
            _lastError = ex.Message;
            return false;
        }
    }

    public void Disconnect()
    {
        try
        {
            _pipe?.Close();
            _pipe?.Dispose();
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

        if (!await ConnectAsync())
        {
            System.Diagnostics.Debug.WriteLine("[IpcService] Connect failed");
            return status;
        }

        try
        {
            var writer = new StreamWriter(_pipe!) { AutoFlush = true };
            var reader = new StreamReader(_pipe!);

            writer.WriteLine("GET_STATUS");

            var firstLine = reader.ReadLine();
            System.Diagnostics.Debug.WriteLine($"[IpcService] GET_STATUS response: {firstLine}");
            if (firstLine != null && firstLine.StartsWith("STATUS:"))
            {
                var parts = firstLine.Split(':');
                System.Diagnostics.Debug.WriteLine($"[IpcService] Parts count: {parts.Length}");
                if (parts.Length >= 5)
                {
                    status.ClientName = parts[1];
                    status.IpAddress = parts[2];
                    status.DaemonStatus = parts[3];
                    if (ulong.TryParse(parts[4], out var pid))
                    {
                        status.ClientPid = (int)pid;
                    }
                    status.ClientBackendStatus = "RUNNING";
                }
                else if (parts.Length >= 4)
                {
                    status.ClientName = parts[1];
                    status.IpAddress = parts[2];
                    status.DaemonStatus = parts[3];
                    status.ClientBackendStatus = "RUNNING";
                }
            }

            Disconnect();
        }
        catch (Exception ex) { 
            System.Diagnostics.Debug.WriteLine($"[IpcService] Exception: {ex.Message}");
        }

        return status;
    }

    public async Task<List<string>> GetWhitelistAsync()
    {
        var whitelist = new List<string>();

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
                    whitelist.Add(line);
                }
            }

            Disconnect();
        }
        catch { }

        return whitelist;
    }

    public async Task<bool> RestartClientAsync()
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

            Disconnect();
            return response == "OK";
        }
        catch
        {
            return false;
        }
    }

    public async Task<bool> PingAsync()
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

            Disconnect();
            return response == "OK";
        }
        catch
        {
            return false;
        }
    }

    public async Task<(string clientLog, string firewallLog)> GetLogPathAsync()
    {
        // Use longer timeout, retry once
        for (int attempt = 0; attempt < 2; attempt++)
        {
            if (await ConnectAsync(2000))
            {
                try
                {
                    var writer = new StreamWriter(_pipe!) { AutoFlush = true };
                    var reader = new StreamReader(_pipe!);

                    writer.WriteLine("GET_LOG_PATH");
                    var response = reader.ReadLine();
                    
                    System.Diagnostics.Debug.WriteLine($"[IpcService] GetLogPathAsync response: {response}");

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
                catch (Exception ex)
                {
                    System.Diagnostics.Debug.WriteLine($"[IpcService] GetLogPathAsync error: {ex.Message}");
                    Disconnect();
                }
            }
            else
            {
                System.Diagnostics.Debug.WriteLine($"[IpcService] GetLogPathAsync: Connect attempt {attempt + 1} failed");
            }
            
            // Small delay before retry
            if (attempt == 0) await Task.Delay(500);
        }
        
        return ("", "");
    }

    public void Dispose()
    {
        Disconnect();
    }
}
