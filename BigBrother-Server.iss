#define Product "Server"
#define ProductName "BigBrother Server"
#define InstallFolder "BigBrother Server"
#define DaemonName "BigBrother Server Daemon.exe"
#define ServiceName "BigBrother Server"
#define ServiceExe "BigBrother Server Daemon.exe"
#ifndef ProductVersion
  #define ProductVersion "1.0.0"
#endif
#ifndef PayloadDir
  #define PayloadDir SourcePath + "dist"
#endif

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
Source: "{#PayloadDir}\{#DaemonName}"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#PayloadDir}\server-frontend\*"; DestDir: "{app}\GUI"; Flags: ignoreversion recursesubdirs createallsubdirs; Excludes: "*.pdb"

[Dirs]
Name: "{userappdata}\BigBrother\logs"
Name: "{app}\logs\server"

[Icons]
Name: "{autodesktop}\{#ProductName}"; Filename: "{app}\GUI\BigBrother {#Product}.exe"; WorkingDir: "{app}\GUI"
Name: "{group}\{#ProductName}"; Filename: "{app}\GUI\BigBrother {#Product}.exe"; WorkingDir: "{app}\GUI"

[InstallDelete]
Type: files; Name: "{app}\uninstall.exe"

[Registry]
Root: HKLM32; Subkey: "Software\Microsoft\Windows\CurrentVersion\Uninstall\{#ProductName}"; Flags: deletekey

#include "installer\service-code.iss"
