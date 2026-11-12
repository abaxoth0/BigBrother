using System.ComponentModel;
using System.ServiceProcess;

namespace frontend.Services;

public static class ClientBackendService
{
    public const string ServiceName = "BigBrother Client";

    public static Task<ClientServiceState> GetStateAsync() => Task.Run(() =>
    {
        try
        {
            using var service = new ServiceController(ServiceName);
            return service.Status switch {
                ServiceControllerStatus.Stopped => ClientServiceState.Stopped,
                ServiceControllerStatus.StartPending => ClientServiceState.Starting,
                ServiceControllerStatus.Running => ClientServiceState.Running,
                ServiceControllerStatus.StopPending => ClientServiceState.Stopping,
                _ => ClientServiceState.Unavailable
            };
        }
        catch (InvalidOperationException error) when (error.InnerException is Win32Exception { NativeErrorCode: 1060 })
        {
            return ClientServiceState.Missing;
        }
        catch { return ClientServiceState.Unavailable; }
    });

    public static Task ControlAsync(string action) => Task.Run(() =>
    {
        using var service = new ServiceController(ServiceName);
        void WaitFor(ServiceControllerStatus state) => service.WaitForStatus(state, TimeSpan.FromSeconds(45));
        void Stop()
        {
            service.Refresh();
            if (service.Status == ServiceControllerStatus.StartPending) WaitFor(ServiceControllerStatus.Running);
            if (service.Status == ServiceControllerStatus.Stopped) return;
            if (service.Status != ServiceControllerStatus.StopPending) service.Stop();
            WaitFor(ServiceControllerStatus.Stopped);
        }
        void Start()
        {
            service.Refresh();
            if (service.Status == ServiceControllerStatus.StopPending) WaitFor(ServiceControllerStatus.Stopped);
            if (service.Status == ServiceControllerStatus.Running) return;
            if (service.Status != ServiceControllerStatus.StartPending) service.Start();
            WaitFor(ServiceControllerStatus.Running);
        }
        switch (action)
        {
            case "start": Start(); break;
            case "stop": Stop(); break;
            case "restart": Stop(); Start(); break;
            default: throw new ArgumentException("Unknown client service action.", nameof(action));
        }
    });
}
