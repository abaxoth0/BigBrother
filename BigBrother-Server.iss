#define Product "Server"
#define NotificationCLSID "79663FA3-2350-46E7-9040-00B879F513DF"
#define NotificationProtocol "bigbrother-server"
#define ProductName "BigBrother Server"
#define InstallFolder "BigBrother Server"
#define DaemonName "BigBrother Server Daemon.exe"
#define ServiceName "BigBrother Server"
#define ServiceExe "BigBrother Server Daemon.exe"
#include "installer\version.iss"
#ifndef PayloadDir
  #define PayloadDir SourcePath + "dist"
#endif
#include "installer\payload-version.iss"

[Setup]
AppId={{8B65ED89-B2C9-42E3-BE1C-3ADB054A5968}
AppName={#ProductName}
AppVersion={#ProductVersion}
DefaultDirName={code:DefaultInstallDir}
DefaultGroupName={#ProductName}
DisableProgramGroupPage=yes
PrivilegesRequired=admin
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
OutputDir=dist\installers
OutputBaseFilename=BigBrother-{#ProductVersion}-{#Product}-Setup
SetupIconFile=assets\icons\BigBrother_Server_Frontend.ico
UninstallDisplayIcon={app}\GUI\BigBrother {#Product}.exe
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
CloseApplications=yes
RestartApplications=no

[Files]
Source: "{#PayloadDir}\BigBrother Notifications.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#PayloadDir}\{#DaemonName}"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#PayloadDir}\server-frontend\BigBrother Server.exe"; DestDir: "{app}\GUI"; Flags: ignoreversion
Source: "{#PayloadDir}\server-frontend\D3DCompiler_47_cor3.dll"; DestDir: "{app}\GUI"; Flags: ignoreversion
Source: "{#PayloadDir}\server-frontend\PenImc_cor3.dll"; DestDir: "{app}\GUI"; Flags: ignoreversion
Source: "{#PayloadDir}\server-frontend\PresentationNative_cor3.dll"; DestDir: "{app}\GUI"; Flags: ignoreversion
Source: "{#PayloadDir}\server-frontend\vcruntime140_cor3.dll"; DestDir: "{app}\GUI"; Flags: ignoreversion
Source: "{#PayloadDir}\server-frontend\wpfgfx_cor3.dll"; DestDir: "{app}\GUI"; Flags: ignoreversion

[Dirs]
Name: "{userappdata}\BigBrother\logs"
Name: "{app}\logs\server"

[Icons]
Name: "{autodesktop}\{#ProductName}"; Filename: "{app}\GUI\BigBrother {#Product}.exe"; WorkingDir: "{app}\GUI"
Name: "{group}\{#ProductName}"; Filename: "{app}\GUI\BigBrother {#Product}.exe"; WorkingDir: "{app}\GUI"; AppUserModelID: "BigBrother.{#Product}"; AppUserModelToastActivatorCLSID: "{#NotificationCLSID}"

[InstallDelete]
Type: files; Name: "{app}\uninstall.exe"
Type: filesandordirs; Name: "{app}\GUI"

[UninstallDelete]
Type: filesandordirs; Name: "{app}\data"
Type: filesandordirs; Name: "{app}\logs"
Type: filesandordirs; Name: "{commonappdata}\BigBrother\data"

[Registry]
Root: HKLM; Subkey: "Software\Classes\AppUserModelId\BigBrother.{#Product}"; ValueType: string; ValueName: "DisplayName"; ValueData: "{#ProductName}"; Flags: uninsdeletekey
Root: HKLM; Subkey: "Software\Classes\AppUserModelId\BigBrother.{#Product}"; ValueType: string; ValueName: "CustomActivator"; ValueData: "{{{#NotificationCLSID}}"
Root: HKLM; Subkey: "Software\Classes\{#NotificationProtocol}"; ValueType: string; ValueData: "URL:{#ProductName}"; Flags: uninsdeletekey
Root: HKLM; Subkey: "Software\Classes\{#NotificationProtocol}"; ValueType: string; ValueName: "URL Protocol"; ValueData: ""
Root: HKLM; Subkey: "Software\Classes\{#NotificationProtocol}\shell\open\command"; ValueType: string; ValueData: """{app}\BigBrother Notifications.exe"" --open {#Product} ""%1"""
Root: HKLM32; Subkey: "Software\Microsoft\Windows\CurrentVersion\Uninstall\{#ProductName}"; Flags: deletekey

#include "installer\service-code.iss"
