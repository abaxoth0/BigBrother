<#
.SYNOPSIS
    Uninstall BigBrother distributed firewall system.
.DESCRIPTION
    Removes all components: services, firewall rules, shortcuts,
    and optionally the data directory.
#>

#Requires -RunAsAdministrator

$ErrorActionPreference = "Stop"

# --- Configuration ---
$AppName     = "BigBrother"
$AppDir      = "$env:ProgramFiles\$AppName"
$DataDir     = "$env:ProgramDATA\$AppName"
$ShortcutDir = [Environment]::GetFolderPath("CommonDesktopDirectory")

$FirewallSvcName = "BigBrother"
$ServerSvcName   = "BigBrotherServer"

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

# 1. Stop services
Write-Step "Stopping services..."
foreach ($svc in @($FirewallSvcName, $ServerSvcName)) {
    $s = Get-Service -Name $svc -ErrorAction SilentlyContinue
    if ($s -and $s.Status -eq "Running") {
        Stop-Service -Name $svc -Force
        Write-Host "  Stopped $svc"
    }
}

# 2. Remove services
Write-Step "Removing services..."
foreach ($svc in @($FirewallSvcName, $ServerSvcName)) {
    sc.exe delete $svc 2>$null
    Write-Host "  Removed $svc"
}

# 3. Remove Windows Firewall rules
Write-Step "Removing Windows Firewall rules..."
netsh advfirewall firewall delete rule name="BigBrother Server (TCP 1984)" 2>$null | Out-Null
netsh advfirewall firewall delete rule name="BigBrother Discovery (UDP 42069)" 2>$null | Out-Null
netsh advfirewall firewall delete rule name="BigBrother Server (TCP 1984)" 2>$null | Out-Null
netsh advfirewall firewall delete rule name="BigBrother Discovery (UDP 42069)" 2>$null | Out-Null
Write-Host "  Done"

# 4. Remove desktop shortcuts
Write-Step "Removing desktop shortcuts..."
@("BigBrother Client", "BigBrother Server") | ForEach-Object {
    $link = "$ShortcutDir\$_.lnk"
    if (Test-Path $link) {
        Remove-Item $link -Force
        Write-Host "  Removed $_"
    }
}

# 5. Ask about data
Write-Step "Data directory..."
$keepData = $false
$answer = Read-Host "Keep data directory ($DataDir)? [y/N]"
if ($answer -eq "y" -or $answer -eq "Y") {
    $keepData = $true
    Write-Host "  Data kept at $DataDir"
} else {
    Write-Host "  Data will be removed"
}

# 6. Remove program files
Write-Step "Removing program files..."
if (Test-Path $AppDir) {
    Remove-Item $AppDir -Recurse -Force
    Write-Host "  Removed $AppDir"
}

# 7. Remove data (if not keeping)
if (-not $keepData -and (Test-Path $DataDir)) {
    Write-Step "Removing data directory..."
    Remove-Item $DataDir -Recurse -Force
    Write-Host "  Removed $DataDir"
}

Write-Host "`n=== Uninstallation complete ===" -ForegroundColor Green
