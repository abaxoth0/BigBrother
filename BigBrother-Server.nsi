; BigBrother Server Installer
; Build: makensis BigBrother-Server.nsi

Unicode True
RequestExecutionLevel admin

!define PRODUCT_NAME "BigBrother Server"
!define PRODUCT_SHORT "BigBrother"
!define PRODUCT_VERSION "1.0.0"
!define PRODUCT_DIR "$PROGRAMFILES64\${PRODUCT_NAME}"
!define PRODUCT_DATA "$APPDATA\${PRODUCT_SHORT}"

Name "${PRODUCT_NAME} ${PRODUCT_VERSION}"
OutFile "BigBrother-${PRODUCT_VERSION}-Server-Setup.exe"
InstallDir "${PRODUCT_DIR}"

Page instfiles
UninstPage uninstConfirm
UninstPage instfiles

;--------------------------------
; Install sections

Section "Server Backend" SecServer
    SectionIn RO
    SetOutPath "$INSTDIR"
    File /nonfatal "dist\BigBrother Server Daemon.exe"
    CreateDirectory "${PRODUCT_DATA}"
    CreateDirectory "$INSTDIR\logs\server"

    DetailPrint "Removing previous server service..."
    ExecWait '"$SYSDIR\net.exe" stop "BigBrother Server"'
    ExecWait '"$SYSDIR\sc.exe" delete "BigBrother Server"'

    DetailPrint "Creating server service (manual start)..."
    ExecWait '"$SYSDIR\sc.exe" create "BigBrother Server" binPath= "$INSTDIR\BigBrother Server Daemon.exe" start= demand DisplayName= "BigBrother Server"'
    ExecWait '"$SYSDIR\sc.exe" description "BigBrother Server" "Manages whitelists, clients, and serves discovery."'
    ExecWait '"$SYSDIR\net.exe" start "BigBrother Server"'
SectionEnd

Section "Server GUI" SecServerGUI
    SetOutPath "$INSTDIR"
    File /nonfatal /r "dist\server-frontend\*.*"
    CreateShortCut "$DESKTOP\BigBrother Server.lnk" "$INSTDIR\BigBrother Server.exe"
    SetOutPath "$INSTDIR"
SectionEnd

Section "Windows Firewall Rules" SecRules
    DetailPrint "Adding firewall rules..."
    ExecWait '"$SYSDIR\netsh.exe" advfirewall firewall add rule name= "BigBrother Server (TCP 1984)" dir=in action=allow protocol=tcp localport=1984 profile=private,domain'
    ExecWait '"$SYSDIR\netsh.exe" advfirewall firewall add rule name= "BigBrother Discovery (UDP 42069)" dir=in action=allow protocol=udp localport=42069 profile=private,domain'
SectionEnd

Section -PostInstall
    WriteUninstaller "$INSTDIR\uninstall.exe"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${PRODUCT_NAME}" \
        "DisplayName" "${PRODUCT_NAME}"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${PRODUCT_NAME}" \
        "UninstallString" "$INSTDIR\uninstall.exe"
SectionEnd

;--------------------------------
; Uninstaller

Section "Uninstall"
    ExecWait '"$SYSDIR\net.exe" stop "BigBrother Server"'
    ExecWait '"$SYSDIR\sc.exe" delete "BigBrother Server"'
    ExecWait '"$SYSDIR\netsh.exe" advfirewall firewall delete rule name= "BigBrother Server (TCP 1984)"'
    ExecWait '"$SYSDIR\netsh.exe" advfirewall firewall delete rule name= "BigBrother Discovery (UDP 42069)"'
    Delete "$DESKTOP\BigBrother Server.lnk"
    RMDir /r "$INSTDIR"
    MessageBox MB_YESNO "Remove data directory (${PRODUCT_DATA})?" IDNO +2
    RMDir /r "${PRODUCT_DATA}"
SectionEnd
