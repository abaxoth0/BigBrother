using System.Net;
using System.Net.NetworkInformation;
using System.Net.Sockets;

namespace frontend.Services;

public static class ServerNetworkAddresses
{
    public static string GetLocalAddresses()
    {
        try
        {
            var addresses = NetworkInterface.GetAllNetworkInterfaces()
                .Where(adapter => adapter.OperationalStatus == OperationalStatus.Up && adapter.NetworkInterfaceType != NetworkInterfaceType.Loopback)
                .SelectMany(adapter => adapter.GetIPProperties().UnicastAddresses)
                .Select(address => address.Address)
                .Where(address => !IPAddress.IsLoopback(address) &&
                    address.AddressFamily is AddressFamily.InterNetwork or AddressFamily.InterNetworkV6)
                .Select(address => address.ToString()).Distinct().Order().ToList();
            return addresses.Count > 0 ? string.Join(", ", addresses) : "127.0.0.1";
        }
        catch (NetworkInformationException) { return "Адреса недоступны"; }
    }
}
