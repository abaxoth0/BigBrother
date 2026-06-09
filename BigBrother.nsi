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

    CreateDirectory "${PRODUCT_DATA}"
    CreateDirectory "${PRODUCT_DATA}\logs"

    GetFullPathName $0 "$INSTDIR\BigBrother Firewall.exe"
    nsExec::ExecToStack '"net stop BigBrother Firewall"'
    Pop $1
    nsExec::ExecToStack '"sc.exe delete BigBrother Firewall"'
    Pop $1
    nsExec::ExecToStack '"sc.exe create BigBrother Firewall binPath= $0 start= auto DisplayName= BigBrother Firewall"'
    Pop $1
    nsExec::ExecToStack '"sc.exe description BigBrother Firewall Distributed firewall. Filters traffic based on whitelisted domains."'
    Pop $1
    nsExec::ExecToStack '"sc.exe start BigBrother Firewall"'
    Pop $1
SectionEnd

Section "Client Daemon" SecClientDaemon
    SetOutPath "$INSTDIR"
    File /nonfatal "dist\BigBrother Client Daemon.exe"
SectionEnd

Section "Server Backend" SecServer
    SetOutPath "$INSTDIR"
    File /nonfatal "dist\BigBrother Server Daemon.exe"
    CreateDirectory "${PRODUCT_DATA}"

    GetFullPathName $0 "$INSTDIR\BigBrother Server Daemon.exe"
    nsExec::ExecToStack '"net stop BigBrotherServer"'
    Pop $1
    nsExec::ExecToStack '"sc.exe delete BigBrotherServer"'
    Pop $1
    nsExec::ExecToStack '"sc.exe create BigBrotherServer binPath= $0 start= auto DisplayName= BigBrother Server"'
    Pop $1
    nsExec::ExecToStack '"sc.exe description BigBrotherServer Manages whitelists, clients, and serves discovery."'
    Pop $1
    nsExec::ExecToStack '"sc.exe start BigBrotherServer"'
    Pop $1
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
    nsExec::ExecToStack '"netsh advfirewall firewall add rule name= BigBrotherServer dir=in action=allow protocol=tcp localport=1984 profile=private,domain"'
    Pop $1
    nsExec::ExecToStack '"netsh advfirewall firewall add rule name= BigBrotherDiscovery dir=in action=allow protocol=udp localport=42069 profile=private,domain"'
    Pop $1
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
    nsExec::ExecToStack '"net stop BigBrother Firewall"'
    Pop $1
    nsExec::ExecToStack '"sc.exe delete BigBrother Firewall"'
    Pop $1
    nsExec::ExecToStack '"net stop BigBrotherServer"'
    Pop $1
    nsExec::ExecToStack '"sc.exe delete BigBrotherServer"'
    Pop $1
    nsExec::ExecToStack '"netsh advfirewall firewall delete rule name= BigBrotherServer"'
    Pop $1
    nsExec::ExecToStack '"netsh advfirewall firewall delete rule name= BigBrotherDiscovery"'
    Pop $1
    Delete "$DESKTOP\BigBrother Client.lnk"
    Delete "$DESKTOP\BigBrother Server.lnk"
    RMDir /r "$INSTDIR"
    MessageBox MB_YESNO "Remove data directory (${PRODUCT_DATA})?" IDNO +2
    RMDir /r "${PRODUCT_DATA}"
SectionEnd
