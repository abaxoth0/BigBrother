using System.ServiceProcess;

namespace frontend.Services;

public class ServiceInfo
{
    public string Name { get; set; } = "";
    public string DisplayName { get; set; } = "";
    public ServiceControllerStatus Status { get; set; }
    public bool CanStop { get; set; }
    public bool CanPause { get; set; }
}

public class ServiceManager : IDisposable
{
    private const string DefaultServiceName = "BigBrother";

    public string ServiceName { get; set; } = DefaultServiceName;

    public ServiceManager(string? serviceName = null)
    {
        if (!string.IsNullOrEmpty(serviceName))
        {
            ServiceName = serviceName;
        }
    }

    public ServiceInfo? GetServiceInfo()
    {
        try
        {
            using var sc = new ServiceController(ServiceName);
            return new ServiceInfo
            {
                Name = sc.ServiceName,
                DisplayName = sc.DisplayName,
                Status = sc.Status,
                CanStop = sc.CanStop,
                CanPause = sc.CanPauseAndContinue
            };
        }
        catch
        {
            return null;
        }
    }

    public string GetServiceStatus()
    {
        try
        {
            using var sc = new ServiceController(ServiceName);
            return sc.Status.ToString();
        }
        catch
        {
            return "Not Found";
        }
    }

    public bool StartService(string? serviceName = null)
    {
        var name = serviceName ?? ServiceName;
        try
        {
            using var sc = new ServiceController(name);
            if (sc.Status != ServiceControllerStatus.Running)
            {
                sc.Start();
                sc.WaitForStatus(ServiceControllerStatus.Running, TimeSpan.FromSeconds(30));
            }
            return sc.Status == ServiceControllerStatus.Running;
        }
        catch
        {
            return false;
        }
    }

    public bool StopService(string? serviceName = null)
    {
        var name = serviceName ?? ServiceName;
        try
        {
            using var sc = new ServiceController(name);
            if (sc.CanStop && sc.Status == ServiceControllerStatus.Running)
            {
                sc.Stop();
                sc.WaitForStatus(ServiceControllerStatus.Stopped, TimeSpan.FromSeconds(30));
            }
            return sc.Status == ServiceControllerStatus.Stopped;
        }
        catch
        {
            return false;
        }
    }

    public bool RestartService(string? serviceName = null)
    {
        var name = serviceName ?? ServiceName;
        try
        {
            StopService(name);
            return StartService(name);
        }
        catch
        {
            return false;
        }
    }

    public async Task<bool> StartServiceAsync(string? serviceName = null)
    {
        return await Task.Run(() => StartService(serviceName));
    }

    public async Task<bool> StopServiceAsync(string? serviceName = null)
    {
        return await Task.Run(() => StopService(serviceName));
    }

    public async Task<bool> RestartServiceAsync(string? serviceName = null)
    {
        return await Task.Run(() => RestartService(serviceName));
    }

    public void Dispose()
    {
    }
}
