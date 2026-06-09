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

!include "nsExec.nsh"
!include "LogicLib.nsh"

;--------------------------------
; Pages
Page components
Page directory
Page instfiles
UninstPage uninstConfirm
UninstPage instfiles

;--------------------------------
; Install sections

Section "Firewall Service" SecFirewall
    SectionIn RO
    SetOutPath "$INSTDIR"
    File "dist\firewall-service.exe"
    File "dist\WinDivert.dll"

    CreateDirectory "${PRODUCT_DATA}"
    CreateDirectory "${PRODUCT_DATA}\logs"

    nsExec::Exec 'net stop BigBrother'
    nsExec::Exec 'sc.exe delete BigBrother'
    nsExec::Exec 'sc.exe create BigBrother binPath= "$INSTDIR\firewall-service.exe" start= auto DisplayName= "BigBrother Firewall"'
    nsExec::Exec 'sc.exe description BigBrother "Distributed firewall. Filters traffic based on whitelisted domains."'
    nsExec::Exec 'sc.exe start BigBrother'
SectionEnd

Section "Server Backend" SecServer
    SetOutPath "$INSTDIR"
    File "dist\bb-server.exe"

    CreateDirectory "${PRODUCT_DATA}"

    nsExec::Exec 'net stop BigBrotherServer'
    nsExec::Exec 'sc.exe delete BigBrotherServer'
    nsExec::Exec 'sc.exe create BigBrotherServer binPath= "$INSTDIR\bb-server.exe" start= auto DisplayName= "BigBrother Server"'
    nsExec::Exec 'sc.exe description BigBrotherServer "Manages whitelists, clients, and serves discovery."'
    nsExec::Exec 'sc.exe start BigBrotherServer'
SectionEnd

Section "Client Backend" SecClient
    SetOutPath "$INSTDIR"
    File "dist\bb-client.exe"
SectionEnd

Section "Client Frontend" SecClientGUI
    SetOutPath "$INSTDIR"
    File /r "dist\client-frontend\*.*"
    CreateShortCut "$DESKTOP\BigBrother Client.lnk" "$INSTDIR\bb-client-gui.exe"
SectionEnd

Section "Server Frontend" SecServerGUI
    SetOutPath "$INSTDIR"
    File /r "dist\server-frontend\*.*"
    CreateShortCut "$DESKTOP\BigBrother Server.lnk" "$INSTDIR\bb-server-gui.exe"
SectionEnd

Section "Firewall Rules" SecRules
    nsExec::Exec 'netsh advfirewall firewall add rule name="BigBrother Server (TCP 1984)" dir=in action=allow protocol=tcp localport=1984 profile=private,domain'
    nsExec::Exec 'netsh advfirewall firewall add rule name="BigBrother Discovery (UDP 42069)" dir=in action=allow protocol=udp localport=42069 profile=private,domain'
SectionEnd

;--------------------------------
; Uninstaller
Section "Uninstall"
    nsExec::Exec 'net stop BigBrother'
    nsExec::Exec 'sc.exe delete BigBrother'
    nsExec::Exec 'net stop BigBrotherServer'
    nsExec::Exec 'sc.exe delete BigBrotherServer'
    nsExec::Exec 'netsh advfirewall firewall delete rule name="BigBrother Server (TCP 1984)"'
    nsExec::Exec 'netsh advfirewall firewall delete rule name="BigBrother Discovery (UDP 42069)"'
    Delete "$DESKTOP\BigBrother Client.lnk"
    Delete "$DESKTOP\BigBrother Server.lnk"
    RMDir /r "$INSTDIR"
    MessageBox MB_YESNO "Remove data directory ($PRODUCT_DATA)?" IDNO SkipData
    RMDir /r "$PRODUCT_DATA"
    SkipData:
SectionEnd
