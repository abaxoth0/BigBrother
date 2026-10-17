#ifndef ProductVersion
  #define ProductVersion "1.0.0"
#endif
#ifndef PayloadDir
  #define PayloadDir SourcePath + "dist"
#endif

[Setup]
AppId=BigBrother.Client.Update.Payload
AppName=BigBrother Client Update
AppVersion={#ProductVersion}
DefaultDirName={tmp}\BigBrotherUpdatePayload
UsePreviousAppDir=no
Uninstallable=no
CreateUninstallRegKey=no
PrivilegesRequired=admin
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
OutputDir=dist\installers
OutputBaseFilename=BigBrother-{#ProductVersion}-Client-Update
Compression=lzma2
SolidCompression=yes
DisableDirPage=yes
DisableProgramGroupPage=yes
DisableReadyPage=yes
CloseApplications=no
RestartApplications=no

[Files]
Source: "{#PayloadDir}\BigBrother Notifications.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#PayloadDir}\BigBrother Client Daemon.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#PayloadDir}\BigBrother Firewall.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#PayloadDir}\WinDivert.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#PayloadDir}\WinDivert64.sys"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#PayloadDir}\client-version.txt"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#PayloadDir}\client-frontend\BigBrother Client.exe"; DestDir: "{app}\GUI"; Flags: ignoreversion
Source: "{#PayloadDir}\client-frontend\D3DCompiler_47_cor3.dll"; DestDir: "{app}\GUI"; Flags: ignoreversion
Source: "{#PayloadDir}\client-frontend\PenImc_cor3.dll"; DestDir: "{app}\GUI"; Flags: ignoreversion
Source: "{#PayloadDir}\client-frontend\PresentationNative_cor3.dll"; DestDir: "{app}\GUI"; Flags: ignoreversion
Source: "{#PayloadDir}\client-frontend\vcruntime140_cor3.dll"; DestDir: "{app}\GUI"; Flags: ignoreversion
Source: "{#PayloadDir}\client-frontend\wpfgfx_cor3.dll"; DestDir: "{app}\GUI"; Flags: ignoreversion

[Code]
function GetDriveType(RootPath: String): LongWord;
  external 'GetDriveTypeW@kernel32.dll stdcall';
function GetFileAttributes(Path: String): LongWord;
  external 'GetFileAttributesW@kernel32.dll stdcall';

function LocalPath(Path: String): Boolean;
begin
  Result := False;
  if (Length(Path) < 3) or (Copy(Path, 2, 2) <> ':\') then Exit;
  if GetDriveType(Copy(Path, 1, 3)) <> 3 then Exit;
  while Length(Path) > 3 do
  begin
    if (GetFileAttributes(Path) and $400) <> 0 then Exit;
    Path := ExtractFileDir(Path);
  end;
  Result := True;
end;

function InitializeSetup(): Boolean;
begin
  Result := IsAdmin and LocalPath(ExpandConstant('{srcexe}')) and
    (ExpandConstant('{param:EXTRACTONLY|}') = '1') and
    LocalPath(ExpandConstant('{param:DIR|}'));
end;
