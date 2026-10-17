using System.Reflection;

namespace frontend;

public static class BuildVersion
{
    public static string Current => (Assembly.GetEntryAssembly() ?? typeof(BuildVersion).Assembly)
        .GetCustomAttribute<AssemblyInformationalVersionAttribute>()!.InformationalVersion;
}
