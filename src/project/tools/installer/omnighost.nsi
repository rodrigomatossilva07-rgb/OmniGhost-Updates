; OmniGhost NSIS Installer
; Professional installer with modern UI

!include MUI2.nsh
!include LogicLib.nsh
!include x64.nsh
!include WinVer.nsh

; =============================================================================
; Basic Configuration
; =============================================================================

!define PRODUCT_NAME "OmniGhost"
!define PRODUCT_VERSION "1.3.4"
!define PRODUCT_PUBLISHER "OmniGhost Team"
!define PRODUCT_WEB_SITE "https://omnighost.dev"
!define PRODUCT_DIR_REGKEY "Software\Microsoft\Windows\CurrentVersion\App Paths\OmniGhost.exe"
!define PRODUCT_UNINST_KEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\OmniGhost"
!define PRODUCT_UNINST_ROOT_KEY "HKCU"

; Build configurations
!ifndef BUILD_TYPE
!define BUILD_TYPE "Publish"
!endif

!define INSTALLER_NAME "OmniGhost-${PRODUCT_VERSION}-${BUILD_TYPE}-Setup.exe"
!define OUTPUT_DIR "..\..\..\..\build\${BUILD_TYPE}\installer"

; =============================================================================
; Request Execution Level
; =============================================================================

RequestExecutionLevel admin
InstallDir "$LOCALAPPDATA\OmniGhost"
InstallDirRegKey HKCU "${PRODUCT_UNINST_KEY}" ""

; =============================================================================
; UI Settings
; =============================================================================

!define MUI_ABORTWARNING
!define MUI_ICON "${NSISDIR}\Contrib\Graphics\Icons\modern-install.ico"
!define MUI_UNICON "${NSISDIR}\Contrib\Graphics\Icons\modern-uninstall.ico"

!define MUI_WELCOMEPAGE_TITLE "Welcome to the OmniGhost Setup Wizard"
!define MUI_WELCOMEPAGE_TEXT "This wizard will guide you through the installation of OmniGhost ${PRODUCT_VERSION}.\n\nClick Next to continue."

!define MUI_LICENSEPAGE_TEXT "Please review the license agreement before installing OmniGhost."
!define MUI_LICENSEPAGE_CHECKBOX "I accept the terms of the license agreement"

!define MUI_DIRECTORYPAGE_TEXT "Choose the installation directory for OmniGhost."
!define MUI_DIRECTORYPAGE_VARIABLE $INSTDIR

!define MUI_COMPONENTSPAGE_TEXT "Select the components you want to install."

!define MUI_INSTFILESPAGE_TITLE "Installing OmniGhost"
!define MUI_INSTFILESPAGE_TEXT "Please wait while OmniGhost is being installed."

!define MUI_FINISHPAGE_TITLE "Installation Complete"
!define MUI_FINISHPAGE_TEXT "OmniGhost has been successfully installed.\n\nClick Finish to launch OmniGhost."
!define MUI_FINISHPAGE_RUN "OmniGhost.exe"
!define MUI_FINISHPAGE_RUN_NOTCHECKED "Launch OmniGhost"
!define MUI_FINISHPAGE_SHOWREADME ""

!define MUI_UNWELCOMEPAGE_TITLE "Uninstall OmniGhost"
!define MUI_UNWELCOMEPAGE_TEXT "This wizard will guide you through the uninstallation of OmniGhost."

!define MUI_UNCONFIRMPAGE_TITLE "Confirm Uninstallation"
!define MUI_UNCONFIRMPAGE_TEXT "Are you sure you want to uninstall OmniGhost?"

!define MUI_UNINSTFILESPAGE_TITLE "Uninstalling OmniGhost"
!define MUI_UNINSTFILESPAGE_TEXT "Please wait while OmniGhost is being uninstalled."

!define MUI_UNFINISHPAGE_TITLE "Uninstallation Complete"
!define MUI_UNFINISHPAGE_TEXT "OmniGhost has been successfully uninstalled."

; =============================================================================
; Pages
; =============================================================================

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_LICENSE "${NSISDIR}\Docs\license.txt"
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_COMPONENTS
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_WELCOME
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_UNPAGE_FINISH

; =============================================================================
; Languages
; =============================================================================

!insertmacro MUI_LANGUAGE "English"
!insertmacro MUI_LANGUAGE "PortugueseBR"
!insertmacro MUI_LANGUAGE "Spanish"
!insertmacro MUI_LANGUAGE "French"
!insertmacro MUI_LANGUAGE "German"
!insertmacro MUI_LANGUAGE "Russian"

; =============================================================================
; Reserved Files
; =============================================================================

ReserveFile "${NSISDIR}\Plugins\x86-ansi\System.dll"
ReserveFile "${NSISDIR}\Plugins\x86-ansi\AdvSplash.dll"

; =============================================================================
; Sections
; =============================================================================

Section "Main Application" SEC_MAIN
    SectionIn RO

    SetOutPath "$INSTDIR"
    File /r "${OUTPUT_DIR}\OmniGhost.exe"
    File /r "${OUTPUT_DIR}\*.dll"
    File /r "${OUTPUT_DIR}\*.dat"
    File /r "${OUTPUT_DIR}\resources\*"

    ; Create uninstaller
    WriteUninstaller "$INSTDIR\Uninstall.exe"

    ; Create registry keys
    WriteRegStr HKCU "${PRODUCT_UNINST_KEY}" "DisplayName" "${PRODUCT_NAME}"
    WriteRegStr HKCU "${PRODUCT_UNINST_KEY}" "DisplayVersion" "${PRODUCT_VERSION}"
    WriteRegStr HKCU "${PRODUCT_UNINST_KEY}" "Publisher" "${PRODUCT_PUBLISHER}"
    WriteRegStr HKCU "${PRODUCT_UNINST_KEY}" "URLInfoAbout" "${PRODUCT_WEB_SITE}"
    WriteRegStr HKCU "${PRODUCT_UNINST_KEY}" "UninstallString" "$INSTDIR\Uninstall.exe"
    WriteRegStr HKCU "${PRODUCT_UNINST_KEY}" "QuietUninstallString" "$INSTDIR\Uninstall.exe /S"
    WriteRegStr HKCU "${PRODUCT_UNINST_KEY}" "InstallLocation" "$INSTDIR"
    WriteRegStr HKCU "${PRODUCT_UNINST_KEY}" "DisplayIcon" "$INSTDIR\OmniGhost.exe"

    WriteRegStr HKCU "${PRODUCT_DIR_REGKEY}" "" "$INSTDIR\OmniGhost.exe"

    ; Create start menu shortcuts
    CreateDirectory "$SMPROGRAMS\OmniGhost"
    CreateShortcut "$SMPROGRAMS\OmniGhost\OmniGhost.lnk" "$INSTDIR\OmniGhost.exe" "" "$INSTDIR\OmniGhost.exe" 0
    CreateShortcut "$SMPROGRAMS\OmniGhost\Uninstall.lnk" "$INSTDIR\Uninstall.exe" "" "$INSTDIR\Uninstall.exe" 0
    CreateShortcut "$SMPROGRAMS\OmniGhost\Website.lnk" "https://omnighost.dev" "" "" 0
    CreateShortcut "$SMPROGRAMS\OmniGhost\Discord.lnk" "https://discord.gg/omnighost" "" "" 0

    ; Desktop shortcut
    CreateShortcut "$DESKTOP\OmniGhost.lnk" "$INSTDIR\OmniGhost.exe" "" "$INSTDIR\OmniGhost.exe" 0

    ; File associations (if needed)
    ; WriteRegStr HKCU "Software\Classes\OmniGhost.Config" "" "OmniGhost Configuration File"
    ; WriteRegStr HKCU "Software\Classes\OmniGhost.Config\DefaultIcon" "" "$INSTDIR\OmniGhost.exe,0"
    ; WriteRegStr HKCU "Software\Classes\OmniGhost.Config\shell\open\command" "" "$INSTDIR\OmniGhost.exe \"%1\""

SectionEnd

Section "Additional Components" SEC_ADDITIONAL
    SectionIn 1

    ; Additional resources
    File /r "${OUTPUT_DIR}\resources\*"
    
    ; Config templates
    File /r "${OUTPUT_DIR}\config\*"

SectionEnd

Section "Desktop Shortcut" SEC_DESKTOP
    SectionIn 1
    CreateShortcut "$DESKTOP\OmniGhost.lnk" "$INSTDIR\OmniGhost.exe" "" "$INSTDIR\OmniGhost.exe" 0
SectionEnd

Section "Start Menu Shortcuts" SEC_STARTMENU
    SectionIn 1
    CreateShortcut "$SMPROGRAMS\OmniGhost\OmniGhost.lnk" "$INSTDIR\OmniGhost.exe" "" "$INSTDIR\OmniGhost.exe" 0
    CreateShortcut "$SMPROGRAMS\OmniGhost\Uninstall.lnk" "$INSTDIR\Uninstall.exe" "" "$INSTDIR\Uninstall.exe" 0
    CreateShortcut "$SMPROGRAMS\OmniGhost\Website.lnk" "https://omnighost.dev" "" "" 0
SectionEnd

; =============================================================================
; Uninstall Section
; =============================================================================

Section "Uninstall"
    ; Remove files
    RMDir /r "$INSTDIR"

    ; Remove shortcuts
    Delete "$SMPROGRAMS\OmniGhost\OmniGhost.lnk"
    Delete "$SMPROGRAMS\OmniGhost\Uninstall.lnk"
    Delete "$SMPROGRAMS\OmniGhost\Website.lnk"
    Delete "$SMPROGRAMS\OmniGhost\Discord.lnk"
    RMDir "$SMPROGRAMS\OmniGhost"

    Delete "$DESKTOP\OmniGhost.lnk"

    ; Remove registry keys
    DeleteRegKey HKCU "${PRODUCT_UNINST_KEY}"
    DeleteRegKey HKCU "${PRODUCT_DIR_REGKEY}"

    ; Remove file associations
    ; DeleteRegKey HKCU "Software\Classes\OmniGhost.Config"

SectionEnd

; =============================================================================
; Functions
; =============================================================================

Function .onInit
    ; Check Windows version
    ${If} ${AtLeastWin10} == 0
        MessageBox MB_OK|MB_ICONSTOP "OmniGhost requires Windows 10 or later."
        Abort
    ${EndIf}

    ; Check if already running
    ${GetParameters} $0
    StrCmp $0 "/S" 0 +2
    ; Silent install
    ${EndIf}

    ; Check for existing installation
    ReadRegStr $0 HKCU "${PRODUCT_UNINST_KEY}" "DisplayVersion"
    StrCmp $0 "" +3
        MessageBox MB_YESNO|MB_ICONQUESTION "OmniGhost is already installed (v$0).\nDo you want to reinstall?" IDNO +2
        Abort
FunctionEnd

Function .onInstSuccess
    ; Create registry entry for auto-start (optional)
    ; WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Run" "OmniGhost" '"$INSTDIR\OmniGhost.exe" --minimized'
FunctionEnd

Function un.onInit
    ; Check if running
    FindWindow $0 "OmniGhost"
    ${If} $0 != 0
        MessageBox MB_OK|MB_ICONEXCLAMATION "OmniGhost is currently running. Please close it before uninstalling."
        Abort
    ${EndIf}
FunctionEnd

Function un.onUninstSuccess
    HideWindow
    MessageBox MB_OK|MB_ICONINFORMATION "OmniGhost has been successfully uninstalled."
FunctionEnd

; =============================================================================
; Custom Pages (Optional)
; =============================================================================

; Custom welcome page
Function customWelcomePage
    !insertmacro MUI_HEADER_TEXT "Welcome to OmniGhost Setup" "Installing OmniGhost ${PRODUCT_VERSION}"
FunctionEnd

; Custom directory page
Function customDirectoryPage
    !insertmacro MUI_HEADER_TEXT "Choose Install Location" "Select the destination folder for OmniGhost."
FunctionEnd

; =============================================================================
; Modern UI Hooks
; =============================================================================

!define MUI_CUSTOMFUNCTION_PRE .onInit
!define MUI_CUSTOMFUNCTION_POST .onInstSuccess
!define MUI_UNCUSTOMFUNCTION_PRE un.onInit
!define MUI_UNCUSTOMFUNCTION_POST un.onUninstSuccess

; =============================================================================
; Build Configuration
; =============================================================================

; Output
OutFile "${OUTPUT_DIR}\${INSTALLER_NAME}"

; Compression
SetCompressor /SOLID zlib
SetCompress auto
SetDatablockOptimize on

; Silent install support
!define MUI_STARTMENUPAGE_REGISTRY_ROOT "HKCU"
!define MUI_STARTMENUPAGE_REGISTRY_KEY "Software\OmniGhost"
!define MUI_STARTMENUPAGE_REGISTRY_VALUENAME "StartMenuFolder"

; Version info
VIProductVersion "${PRODUCT_VERSION}"
VIAddVersionKey "ProductName" "${PRODUCT_NAME}"
VIAddVersionKey "CompanyName" "${PRODUCT_PUBLISHER}"
VIAddVersionKey "LegalCopyright" "Copyright (C) 2026 ${PRODUCT_PUBLISHER}"
VIAddVersionKey "FileVersion" "${PRODUCT_VERSION}"
VIAddVersionKey "ProductVersion" "${PRODUCT_VERSION}"
VIAddVersionKey "FileDescription" "OmniGhost DMA Overlay"
VIAddVersionKey "OriginalFilename" "OmniGhost.exe"
VIAddVersionKey "InternalName" "OmniGhost"

; =============================================================================
; CRCCheck
; =============================================================================

CRCCheck on

; =============================================================================
; Silent Install Support
; =============================================================================

!define MUI_SILENT_INSTALL_SUPPORTED
!define MUI_SILENT_UNINSTALL_SUPPORTED

; =============================================================================
; License Data
; =============================================================================

; LicenseData "license.txt"

; =============================================================================
; Build Types
; =============================================================================

!if "${BUILD_TYPE}" == "Publish"
    !define BUILD_SUFFIX ""
!elseif "${BUILD_TYPE}" == "Release"
    !define BUILD_SUFFIX "-beta"
!elseif "${BUILD_TYPE}" == "Development"
    !define BUILD_SUFFIX "-dev"
!else
    !define BUILD_SUFFIX "-debug"
!endif

; Final output name
!define INSTALLER_NAME "OmniGhost-${PRODUCT_VERSION}${BUILD_SUFFIX}-Setup.exe"
