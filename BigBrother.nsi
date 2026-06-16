; BigBrother NSIS Installer
; Build: makensis BigBrother.nsi

Unicode True
RequestExecutionLevel admin

!define PRODUCT_NAME "BigBrother"
!define PRODUCT_VERSION "1.0.0"
!define PRODUCT_DIR "$PROGRAMFILES64\${PRODUCT_NAME}"
!define PRODUCT_DATA "$APPDATA\${PRODUCT_NAME}"

Name "${PRODUCT_NAME} ${PRODUCT_VERSION}"
OutFile "BigBrother-${PRODUCT_VERSION}-Setup.exe"
InstallDir "${PRODUCT_DIR}"

; Pages
Page components
Page directory
Page instfiles
UninstPage uninstConfirm
UninstPage instfiles

;--------------------------------
; Install sections

Section "Firewall Daemon" SecFirewall
    SectionIn RO
    SetOutPath "$INSTDIR"
    File /nonfatal "dist\BigBrother Firewall.exe"
    File /nonfatal "dist\WinDivert.dll"
    File /nonfatal "dist\WinDivert64.sys"
    File /nonfatal "dist\WinDivert.lib"

    CreateDirectory "${PRODUCT_DATA}"
    CreateDirectory "${PRODUCT_DATA}\logs"

    DetailPrint "Installing firewall service..."
    ExecWait '"$SYSDIR\net.exe" stop "BigBrother Firewall"'
    ExecWait '"$SYSDIR\sc.exe" delete "BigBrother Firewall"'
    ExecWait '"$SYSDIR\sc.exe" create "BigBrother Firewall" binPath= "$INSTDIR\BigBrother Firewall.exe" start= auto DisplayName= "BigBrother Firewall"'
    ExecWait '"$SYSDIR\sc.exe" description "BigBrother Firewall" "Distributed firewall. Filters traffic based on whitelisted domains."'
    ExecWait '"$SYSDIR\net.exe" start "BigBrother Firewall"'
SectionEnd

Section "Client Daemon" SecClientDaemon
    SetOutPath "$INSTDIR"
    File /nonfatal "dist\BigBrother Client Daemon.exe"
SectionEnd

Section "Server Backend" SecServer
    SetOutPath "$INSTDIR"
    File /nonfatal "dist\BigBrother Server Daemon.exe"
    CreateDirectory "${PRODUCT_DATA}"

    DetailPrint "Installing server service..."
    ExecWait '"$SYSDIR\net.exe" stop "BigBrotherServer"'
    ExecWait '"$SYSDIR\sc.exe" delete "BigBrotherServer"'
    ExecWait '"$SYSDIR\sc.exe" create "BigBrotherServer" binPath= "$INSTDIR\BigBrother Server Daemon.exe" start= auto DisplayName= "BigBrother Server"'
    ExecWait '"$SYSDIR\sc.exe" description "BigBrotherServer" "Manages whitelists, clients, and serves discovery."'
    ExecWait '"$SYSDIR\net.exe" start "BigBrotherServer"'
SectionEnd

Section "Client GUI" SecClientGUI
    SetOutPath "$INSTDIR\GUI\Client"
    File /nonfatal /r "dist\client-frontend\*.*"
    CreateShortCut "$DESKTOP\BigBrother Client.lnk" "$INSTDIR\GUI\Client\BigBrother Client.exe"
    SetOutPath "$INSTDIR"
SectionEnd

Section "Server GUI" SecServerGUI
    SetOutPath "$INSTDIR\GUI\Server"
    File /nonfatal /r "dist\server-frontend\*.*"
    CreateShortCut "$DESKTOP\BigBrother Server.lnk" "$INSTDIR\GUI\Server\BigBrother Server.exe"
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
    ExecWait '"$SYSDIR\net.exe" stop "BigBrother Firewall"'
    ExecWait '"$SYSDIR\sc.exe" delete "BigBrother Firewall"'
    ExecWait '"$SYSDIR\net.exe" stop "BigBrotherServer"'
    ExecWait '"$SYSDIR\sc.exe" delete "BigBrotherServer"'
    ExecWait '"$SYSDIR\netsh.exe" advfirewall firewall delete rule name= "BigBrother Server (TCP 1984)"'
    ExecWait '"$SYSDIR\netsh.exe" advfirewall firewall delete rule name= "BigBrother Discovery (UDP 42069)"'
    Delete "$DESKTOP\BigBrother Client.lnk"
    Delete "$DESKTOP\BigBrother Server.lnk"
    RMDir /r "$INSTDIR"
    MessageBox MB_YESNO "Remove data directory (${PRODUCT_DATA})?" IDNO +2
    RMDir /r "${PRODUCT_DATA}"
SectionEnd
