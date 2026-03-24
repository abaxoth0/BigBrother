using System.IO;
using System.IO.Pipes;

namespace frontend.Services;

public class ClientStatus
{
    public string ClientName { get; set; } = "";
    public string IpAddress { get; set; } = "";
    public string DaemonStatus { get; set; } = "NOT_RUNNING";
    public string ClientBackendStatus { get; set; } = "NOT_RUNNING";
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
            
            var response = reader.ReadToEnd();
            return response.Trim();
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
            return status;
        }

        try
        {
            var writer = new StreamWriter(_pipe!) { AutoFlush = true };
            var reader = new StreamReader(_pipe!);

            writer.WriteLine("GET_STATUS");

            var firstLine = reader.ReadLine();
            if (firstLine != null && firstLine.StartsWith("STATUS:"))
            {
                var parts = firstLine.Split(':');
                if (parts.Length >= 4)
                {
                    status.ClientName = parts[1];
                    status.IpAddress = parts[2];
                    status.DaemonStatus = parts[3];
                    status.ClientBackendStatus = "RUNNING";
                }
            }

            Disconnect();
        }
        catch { }

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

    public void Dispose()
    {
        Disconnect();
    }
}
