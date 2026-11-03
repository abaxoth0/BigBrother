[Tasks]
#if Product == "Client"
Name: serviceautomatic; Description: "Automatic (when Windows starts)"; GroupDescription: "Service startup:"; Flags: exclusive
Name: servicemanual; Description: "Manual (on demand)"; GroupDescription: "Service startup:"; Flags: exclusive unchecked
#else
Name: servicemanual; Description: "Manual (on demand)"; GroupDescription: "Service startup:"; Flags: exclusive
Name: serviceautomatic; Description: "Automatic (when Windows starts)"; GroupDescription: "Service startup:"; Flags: exclusive unchecked
#endif

[Code]
type
  TServiceStatus = record
    ServiceType: LongWord;
    CurrentState: LongWord;
    ControlsAccepted: LongWord;
    Win32ExitCode: LongWord;
    ServiceSpecificExitCode: LongWord;
    CheckPoint: LongWord;
    WaitHint: LongWord;
  end;

var
  PostInstallFailed: Boolean;

function OpenSCManager(MachineName, DatabaseName, Access: LongWord): LongWord;
  external 'OpenSCManagerW@advapi32.dll stdcall';
function OpenService(Manager: LongWord; Name: String; Access: LongWord): LongWord;
  external 'OpenServiceW@advapi32.dll stdcall';
function QueryServiceStatus(Service: LongWord; var Status: TServiceStatus): Boolean;
  external 'QueryServiceStatus@advapi32.dll stdcall';
function ControlService(Service, Control: LongWord; var Status: TServiceStatus): Boolean;
  external 'ControlService@advapi32.dll stdcall';
function DeleteService(Service: LongWord): Boolean;
  external 'DeleteService@advapi32.dll stdcall';
function CloseServiceHandle(Handle: LongWord): Boolean;
  external 'CloseServiceHandle@advapi32.dll stdcall';

function DefaultInstallDir(Param: String): String;
var
  OldUninstaller: String;
begin
  Result := ExpandConstant('{autopf}\{#InstallFolder}');
  if RegQueryStringValue(HKLM32,
    'Software\Microsoft\Windows\CurrentVersion\Uninstall\{#ProductName}',
    'UninstallString', OldUninstaller) then
    Result := ExtractFileDir(RemoveQuotes(OldUninstaller));
end;

procedure StopAndRemoveService;
var
  Manager, Service: LongWord;
  Status: TServiceStatus;
  Attempt, Error: Integer;
begin
  Manager := OpenSCManager(0, 0, 1);
  if Manager = 0 then
    RaiseException('Cannot open the Windows service manager: ' + SysErrorMessage(DLLGetLastError));
  try
    Service := OpenService(Manager, '{#ServiceName}', $10024);
    if Service = 0 then
    begin
      Error := DLLGetLastError;
      if Error = 1060 then Exit;
      RaiseException('Cannot open {#ServiceName}: ' + SysErrorMessage(Error));
    end;
    try
      if not QueryServiceStatus(Service, Status) then
        RaiseException('Cannot query {#ServiceName}.');
      if Status.CurrentState <> 1 then
      begin
        if Status.CurrentState <> 3 then
          if not ControlService(Service, 1, Status) then
            RaiseException('Cannot stop {#ServiceName}: ' + SysErrorMessage(DLLGetLastError));
        for Attempt := 1 to 300 do
        begin
          if not QueryServiceStatus(Service, Status) then
            RaiseException('Cannot query {#ServiceName}.');
          if Status.CurrentState = 1 then Break;
          Sleep(100);
        end;
        if Status.CurrentState <> 1 then
          RaiseException('{#ServiceName} did not stop within 30 seconds.');
      end;
      if not DeleteService(Service) then
        RaiseException('Cannot delete {#ServiceName}: ' + SysErrorMessage(DLLGetLastError));
    finally
      CloseServiceHandle(Service);
    end;
    for Attempt := 1 to 300 do
    begin
      Service := OpenService(Manager, '{#ServiceName}', 4);
      if Service = 0 then
      begin
        Error := DLLGetLastError;
        if Error = 1060 then Exit;
        if Error <> 1072 then
          RaiseException('Cannot verify service removal: ' + SysErrorMessage(Error));
      end
      else
        CloseServiceHandle(Service);
      Sleep(100);
    end;
    RaiseException('{#ServiceName} is still marked for deletion. Close Services and retry.');
  finally
    CloseServiceHandle(Manager);
  end;
end;

procedure RunRequired(const Filename, Parameters, ErrorMessage: String);
var
  ExitCode: Integer;
begin
  Log('Running: ' + Filename + ' ' + Parameters);
  if not Exec(Filename, Parameters, ExpandConstant('{app}'), SW_HIDE, ewWaitUntilTerminated, ExitCode) then
    RaiseException(ErrorMessage + ' ' + SysErrorMessage(ExitCode));
  if ExitCode <> 0 then
    RaiseException(ErrorMessage + ' Exit code: ' + IntToStr(ExitCode));
end;

procedure RemoveFirewallRule(const Name: String);
var
  ExitCode: Integer;
begin
  Exec(ExpandConstant('{sys}\netsh.exe'),
    'advfirewall firewall delete rule name="' + Name + '"', '',
    SW_HIDE, ewWaitUntilTerminated, ExitCode);
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
begin
  Result := '';
  try
    StopAndRemoveService;
  except
    Result := GetExceptionMessage;
  end;
end;

procedure CurStepChanged(CurStep: TSetupStep);
var
  ServiceStartType: String;
begin
  if CurStep <> ssPostInstall then Exit;
  PostInstallFailed := True;
  RunRequired(ExpandConstant('{app}\{#DaemonName}'), '--secure-storage',
    'Secret permissions setup failed. The service was not started.');
  if WizardIsTaskSelected('serviceautomatic') then
    ServiceStartType := 'auto'
  else
    ServiceStartType := 'demand';
  RunRequired(ExpandConstant('{sys}\sc.exe'),
    'create "{#ServiceName}" binPath= "\"' + ExpandConstant('{app}\{#ServiceExe}') +
    '\"" start= ' + ServiceStartType + ' DisplayName= "{#ServiceName}"',
    'Cannot create {#ServiceName}.');
#if Product == "Client"
  RunRequired(ExpandConstant('{sys}\sc.exe'),
    'description "{#ServiceName}" "Distributed firewall. Filters traffic based on whitelisted domains."',
    'Cannot set the service description.');
  RemoveFirewallRule('BigBrother Server (TCP 1984)');
  RemoveFirewallRule('BigBrother Discovery (UDP 42069)');
  RunRequired(ExpandConstant('{sys}\netsh.exe'),
    'advfirewall firewall add rule name="BigBrother Server (TCP 1984)" dir=in action=allow protocol=tcp localport=1984 profile=private,domain',
    'Cannot create the TCP firewall rule.');
  RunRequired(ExpandConstant('{sys}\netsh.exe'),
    'advfirewall firewall add rule name="BigBrother Discovery (UDP 42069)" dir=in action=allow protocol=udp localport=42069 profile=private,domain',
    'Cannot create the discovery firewall rule.');
#else
  RunRequired(ExpandConstant('{sys}\sc.exe'),
    'description "{#ServiceName}" "Manages whitelists, clients, and serves discovery."',
    'Cannot set the service description.');
#endif
  PostInstallFailed := False;
end;

function GetCustomSetupExitCode: Integer;
begin
  if PostInstallFailed then Result := 1 else Result := 0;
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var
  ExitCode: Integer;
begin
  if CurUninstallStep = usUninstall then
  begin
    StopAndRemoveService;
    if FileExists(ExpandConstant('{app}\BigBrother Notifications.exe')) then
      if not Exec(ExpandConstant('{app}\BigBrother Notifications.exe'),
        '--unregister-all {#Product}', ExpandConstant('{app}'), SW_HIDE, ewWaitUntilTerminated, ExitCode) then
        Log('Could not launch notification registration cleanup.')
      else if ExitCode <> 0 then
        Log('Notification registration cleanup returned ' + IntToStr(ExitCode));
#if Product == "Server"
    RemoveFirewallRule('BigBrother Server (TCP)');
#endif
    RemoveFirewallRule('BigBrother Server (TCP 1984)');
    RemoveFirewallRule('BigBrother Discovery (UDP 42069)');
  end;
end;
