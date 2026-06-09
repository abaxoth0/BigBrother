<#
.SYNOPSIS
    Install BigBrother distributed firewall system.
.DESCRIPTION
    Installs all components: firewall service, server backend, WinDivert driver,
    Windows Firewall rules, desktop shortcuts, and data directory.
#>

#Requires -RunAsAdministrator

$ErrorActionPreference = "Stop"

# --- Configuration ---
$AppName     = "BigBrother"
$AppDir      = "$env:ProgramFiles\$AppName"
$DataDir     = "$env:ProgramDATA\$AppName"
$LogsDir     = "$DataDir\logs"
$ShortcutDir = [Environment]::GetFolderPath("CommonDesktopDirectory")

$FirewallSvcName = "BigBrother"
$ServerSvcName   = "BigBrotherServer"

$TCPPort  = 1984
$UDPPort  = 42069

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$SourceDir = "$ScriptDir\dist"

# --- Helper functions ---
function Write-Step($msg) {
    Write-Host ">>> $msg" -ForegroundColor Cyan
}

function Assert-Admin {
    $id = [System.Security.Principal.WindowsIdentity]::GetCurrent()
    $p  = New-Object System.Security.Principal.WindowsPrincipal($id)
    if (-not $p.IsInRole([System.Security.Principal.WindowsBuiltInRole]::Administrator)) {
        Write-Host "This script requires administrator privileges." -ForegroundColor Red
        exit 1
    }
}

# --- Main ---
Assert-Admin

# 1. Stop running services
Write-Step "Stopping existing services..."
foreach ($svc in @($FirewallSvcName, $ServerSvcName)) {
    $s = Get-Service -Name $svc -ErrorAction SilentlyContinue
    if ($s -and $s.Status -eq "Running") {
        Stop-Service -Name $svc -Force
        Write-Host "  Stopped $svc"
    }
}

# 2. Create directories
Write-Step "Creating directories..."
New-Item -ItemType Directory -Path $AppDir -Force | Out-Null
New-Item -ItemType Directory -Path $DataDir -Force | Out-Null
New-Item -ItemType Directory -Path $LogsDir -Force | Out-Null
Write-Host "  $AppDir"
Write-Host "  $DataDir"

# 3. Copy files
Write-Step "Copying files..."
if (Test-Path $SourceDir) {
    Copy-Item "$SourceDir\*" $AppDir -Recurse -Force
    Write-Host "  Copied from $SourceDir"
} else {
    Write-Host "  dist directory not found, skipping file copy." -ForegroundColor Yellow
    Write-Host "  Build the project first, then run this script." -ForegroundColor Yellow
}

# 4. Install WinDivert driver
Write-Step "Installing WinDivert driver..."
$wdivert = "$AppDir\WinDivert.dll"
if (Test-Path $wdivert) {
    # WinDivert is a DLL, no driver installation needed for user-mode.
    # The DLL is loaded dynamically by the firewall service.
    Write-Host "  WinDivert.dll found at $wdivert"
} else {
    Write-Host "  WinDivert.dll not found, skipping." -ForegroundColor Yellow
}

# 5. Install services
Write-Step "Installing services..."

# Remove old services first
foreach ($svc in @($FirewallSvcName, $ServerSvcName)) {
    sc.exe delete $svc 2>$null | Out-Null
}

# Firewall service (spawns client backend)
$fwExe = "$AppDir\firewall-service.exe"
if (Test-Path $fwExe) {
    sc.exe create $FirewallSvcName `
        binPath= "`"$fwExe`"" `
        start= auto `
        DisplayName= "BigBrother Firewall" `
        type= own
    sc.exe description $FirewallSvcName "BigBrother distributed firewall service. Filters network traffic based on whitelisted domains."
    Write-Host "  $FirewallSvcName -> $fwExe"
} else {
    Write-Host "  firewall-service.exe not found, skipping." -ForegroundColor Yellow
}

# Server backend
$svExe = "$AppDir\bb-server.exe"
if (Test-Path $svExe) {
    sc.exe create $ServerSvcName `
        binPath= "`"$svExe`"" `
        start= auto `
        DisplayName= "BigBrother Server" `
        type= own
    sc.exe description $ServerSvcName "BigBrother server backend. Manages whitelists, clients, and serves discovery requests."
    Write-Host "  $ServerSvcName -> $svExe"
} else {
    Write-Host "  bb-server.exe not found, skipping." -ForegroundColor Yellow
}

# 6. Windows Firewall rules
Write-Step "Adding Windows Firewall rules..."
$ruleNameTcp = "BigBrother Server (TCP $TCPPort)"
$ruleNameUdp = "BigBrother Discovery (UDP $UDPPort)"

# Remove old rules
netsh advfirewall firewall delete rule name="$ruleNameTcp" 2>$null | Out-Null
netsh advfirewall firewall delete rule name="$ruleNameUdp" 2>$null | Out-Null

# Add new rules
netsh advfirewall firewall add rule `
    name="$ruleNameTcp" `
    dir=in `
    action=allow `
    protocol=tcp `
    localport=$TCPPort `
    profile=private,domain `
    description="Allows BigBrother clients to connect to the server."

netsh advfirewall firewall add rule `
    name="$ruleNameUdp" `
    dir=in `
    action=allow `
    protocol=udp `
    localport=$UDPPort `
    profile=private,domain `
    description="Allows BigBrother UDP server discovery."

Write-Host "  TCP $TCPPort and UDP $UDPPort"

# 7. Desktop shortcuts
Write-Step "Creating desktop shortcuts..."
$shell = New-Object -ComObject WScript.Shell

$clientFrontend = "$AppDir\bb-client.exe"
$serverFrontend = "$AppDir\bb-server-gui.exe"

$shortcuts = @(
    @{Name = "BigBrother Client"; Target = $clientFrontend},
    @{Name = "BigBrother Server"; Target = $serverFrontend}
)

foreach ($sc in $shortcuts) {
    if (Test-Path $sc["Target"]) {
        $link = $shell.CreateShortcut("$ShortcutDir\$($sc['Name']).lnk")
        $link.TargetPath = $sc["Target"]
        $link.WorkingDirectory = $AppDir
        $link.Description = "$($sc['Name'])"
        $link.Save()
        Write-Host "  $($sc['Name'])"
    }
}

# 8. Create config.ini with PROGRAMDATA paths
$configPath = "$AppDir\config\config.ini"
New-Item -ItemType Directory -Path "$AppDir\config" -Force | Out-Null
if (-not (Test-Path $configPath)) {
    @"
; BigBrother configuration file
[server]
port=1984

[network]
auto=true
"@ | Out-File -FilePath $configPath -Encoding ASCII
    Write-Host "  Created default config.ini"
}

# 9. Start services
Write-Step "Starting services..."
foreach ($svc in @($FirewallSvcName, $ServerSvcName)) {
    $s = Get-Service -Name $svc -ErrorAction SilentlyContinue
    if ($s) {
        Start-Service -Name $svc
        Write-Host "  Started $svc"
    }
}

# 10. Create PROGAMDATA marker
$marker = "$DataDir\.installed"
"Installed on $(Get-Date)" | Out-File -FilePath $marker -Encoding ASCII

Write-Host "`n=== Installation complete ===" -ForegroundColor Green
Write-Host "Server: $ServerSvcName" -ForegroundColor Green
Write-Host "Firewall: $FirewallSvcName" -ForegroundColor Green
Write-Host "Data: $DataDir" -ForegroundColor Green
Write-Host "Logs: $LogsDir" -ForegroundColor Green
