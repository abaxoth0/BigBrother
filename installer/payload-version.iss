#if GetStringFileInfo(PayloadDir + "\BigBrother Notifications.exe", "ProductVersion") != ProductVersion
  #error Notification helper version does not match VERSION; rebuild it before packaging
#endif
#if Product == "Client"
  #if GetStringFileInfo(PayloadDir + "\BigBrother Firewall.exe", "ProductVersion") != ProductVersion
    #error Firewall version does not match VERSION; rebuild it before packaging
  #endif
  #if GetStringFileInfo(PayloadDir + "\BigBrother Client Daemon.exe", "ProductVersion") != ProductVersion
    #error Client backend version does not match VERSION; rebuild it before packaging
  #endif
  #if GetStringFileInfo(PayloadDir + "\client-frontend\BigBrother Client.exe", "ProductVersion") != ProductVersion
    #error Client frontend version does not match VERSION; rebuild or publish it in Visual Studio
  #endif
#else
  #if GetStringFileInfo(PayloadDir + "\BigBrother Server Daemon.exe", "ProductVersion") != ProductVersion
    #error Server backend version does not match VERSION; rebuild it before packaging
  #endif
  #if GetStringFileInfo(PayloadDir + "\server-frontend\BigBrother Server.exe", "ProductVersion") != ProductVersion
    #error Server frontend version does not match VERSION; rebuild or publish it in Visual Studio
  #endif
#endif
