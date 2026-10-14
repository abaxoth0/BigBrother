#define Product "Client"
#define ProductName "BigBrother Client"
#define InstallFolder "BigBrother"
#define DaemonName "BigBrother Client Daemon.exe"
#define ServiceName "BigBrother Firewall"
#define ServiceExe "BigBrother Firewall.exe"
#ifndef ProductVersion
  #define ProductVersion "1.0.0"
#endif
#ifndef PayloadDir
  #define PayloadDir SourcePath + "dist"
#endif

[Setup]
AppId={{23D8A0D8-6A0C-4D53-80D8-050C4CD2C1AB}
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
SetupIconFile=assets\icons\BigBrother_Client_Frontend.ico
UninstallDisplayIcon={app}\GUI\BigBrother {#Product}.exe
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
CloseApplications=yes
RestartApplications=no

[Files]
Source: "{#PayloadDir}\{#DaemonName}"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#PayloadDir}\BigBrother Firewall.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#PayloadDir}\WinDivert.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#PayloadDir}\WinDivert64.sys"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#PayloadDir}\client-frontend\*"; DestDir: "{app}\GUI"; Flags: ignoreversion recursesubdirs createallsubdirs; Excludes: "*.pdb"

[Dirs]
Name: "{userappdata}\BigBrother\logs"

[Icons]
Name: "{autodesktop}\{#ProductName}"; Filename: "{app}\GUI\BigBrother {#Product}.exe"; WorkingDir: "{app}\GUI"
Name: "{group}\{#ProductName}"; Filename: "{app}\GUI\BigBrother {#Product}.exe"; WorkingDir: "{app}\GUI"

[InstallDelete]
Type: files; Name: "{app}\uninstall.exe"

[Registry]
Root: HKLM32; Subkey: "Software\Microsoft\Windows\CurrentVersion\Uninstall\{#ProductName}"; Flags: deletekey

#include "installer\service-code.iss"
