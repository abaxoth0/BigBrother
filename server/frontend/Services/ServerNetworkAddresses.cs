using System.Net;
using System.Net.NetworkInformation;
using System.Net.Sockets;

namespace frontend.Services;

public static class ServerNetworkAddresses
{
    public static string GetLocalIpv4Address()
    {
        try
        {
            var addresses = NetworkInterface.GetAllNetworkInterfaces()
                .Where(adapter => adapter.OperationalStatus == OperationalStatus.Up && adapter.NetworkInterfaceType != NetworkInterfaceType.Loopback)
                .OrderByDescending(adapter => adapter.NetworkInterfaceType is NetworkInterfaceType.Ethernet or NetworkInterfaceType.Wireless80211)
                .SelectMany(adapter => adapter.GetIPProperties().UnicastAddresses)
                .Select(address => address.Address)
                .ToList();
            return SelectLocalIpv4(addresses);
        }
        catch (NetworkInformationException) { return "Адреса недоступны"; }
    }

    internal static string SelectLocalIpv4(IEnumerable<IPAddress> addresses) => addresses
        .Where(address => address.AddressFamily == AddressFamily.InterNetwork && !IPAddress.IsLoopback(address) &&
            !address.Equals(IPAddress.Any) && !address.Equals(IPAddress.Broadcast))
        .OrderByDescending(address =>
        {
            var bytes = address.GetAddressBytes();
            return bytes[0] == 10 || (bytes[0] == 172 && bytes[1] is >= 16 and <= 31) || (bytes[0] == 192 && bytes[1] == 168);
        })
        .Select(address => address.ToString()).FirstOrDefault() ?? "127.0.0.1";
}
