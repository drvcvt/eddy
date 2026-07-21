Unicode true

!include "LogicLib.nsh"
!include "MUI2.nsh"
!include "x64.nsh"

!ifndef SourceDir
  !error "SourceDir must point to the staged Eddy deployment"
!endif
!ifndef ProductVersion
  !error "ProductVersion must be defined"
!endif
!ifndef ProductVersionNumeric
  !error "ProductVersionNumeric must contain four numeric components"
!endif
!ifndef LicenseRtf
  !error "LicenseRtf must point to the license file"
!endif
!ifndef OutputFile
  !error "OutputFile must be defined"
!endif

Name "Eddy"
OutFile "${OutputFile}"
InstallDir "$PROGRAMFILES64\Eddy"
InstallDirRegKey HKLM "Software\Eddy" "InstallDir"
RequestExecutionLevel admin
SetCompressor /SOLID lzma
ShowInstDetails show
ShowUninstDetails show
ManifestDPIAware true

VIProductVersion "${ProductVersionNumeric}"
VIAddVersionKey /LANG=1033 "ProductName" "Eddy"
VIAddVersionKey /LANG=1033 "CompanyName" "drvcvt"
VIAddVersionKey /LANG=1033 "FileDescription" "Eddy installer"
VIAddVersionKey /LANG=1033 "FileVersion" "${ProductVersion}"
VIAddVersionKey /LANG=1033 "LegalCopyright" "drvcvt"
VIAddVersionKey /LANG=1033 "ProductVersion" "${ProductVersion}"

!define MUI_ABORTWARNING
!define MUI_ICON "${NSISDIR}\Contrib\Graphics\Icons\modern-install.ico"
!define MUI_UNICON "${NSISDIR}\Contrib\Graphics\Icons\modern-uninstall.ico"

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_LICENSE "${LicenseRtf}"
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_UNPAGE_FINISH

!insertmacro MUI_LANGUAGE "English"

!macro RegisterImageExtension Extension
  WriteRegStr HKLM "Software\Eddy\Capabilities\FileAssociations" "${Extension}" "Eddy.Image"
  WriteRegStr HKLM "Software\Classes\Applications\eddy.exe\SupportedTypes" "${Extension}" ""
!macroend

Section "Eddy" MainSection
  SectionIn RO
  SetShellVarContext all
  SetRegView 64
  SetOutPath "$INSTDIR"
  File /r "${SourceDir}\*.*"
  WriteUninstaller "$INSTDIR\Uninstall.exe"

  WriteRegStr HKLM "Software\Eddy" "InstallDir" "$INSTDIR"
  WriteRegStr HKLM "Software\RegisteredApplications" "Eddy" "Software\Eddy\Capabilities"
  WriteRegStr HKLM "Software\Eddy\Capabilities" "ApplicationName" "Eddy"
  WriteRegStr HKLM "Software\Eddy\Capabilities" "ApplicationDescription" "Fast image annotation editor"
  !insertmacro RegisterImageExtension ".png"
  !insertmacro RegisterImageExtension ".jpg"
  !insertmacro RegisterImageExtension ".jpeg"
  !insertmacro RegisterImageExtension ".bmp"
  !insertmacro RegisterImageExtension ".gif"
  !insertmacro RegisterImageExtension ".webp"
  !insertmacro RegisterImageExtension ".tif"
  !insertmacro RegisterImageExtension ".tiff"

  WriteRegStr HKLM "Software\Classes\Eddy.Image" "" "Image file"
  WriteRegStr HKLM "Software\Classes\Eddy.Image\DefaultIcon" "" "$INSTDIR\eddy.exe,0"
  WriteRegStr HKLM "Software\Classes\Eddy.Image\shell\open\command" "" "$\"$INSTDIR\eddy.exe$\" -f $\"%1$\""
  WriteRegStr HKLM "Software\Classes\Applications\eddy.exe" "FriendlyAppName" "Eddy"
  WriteRegStr HKLM "Software\Classes\Applications\eddy.exe\shell\open\command" "" "$\"$INSTDIR\eddy.exe$\" -f $\"%1$\""

  WriteRegStr HKLM "Software\Classes\SystemFileAssociations\image\shell\Eddy.OpenWith" "MUIVerb" "Open with Eddy"
  WriteRegStr HKLM "Software\Classes\SystemFileAssociations\image\shell\Eddy.OpenWith" "Icon" "$INSTDIR\eddy.exe,0"
  WriteRegStr HKLM "Software\Classes\SystemFileAssociations\image\shell\Eddy.OpenWith" "MultiSelectModel" "Single"
  WriteRegStr HKLM "Software\Classes\SystemFileAssociations\image\shell\Eddy.OpenWith\command" "" "$\"$INSTDIR\eddy.exe$\" -f $\"%1$\""

  CreateDirectory "$SMPROGRAMS\Eddy"
  CreateShortcut "$SMPROGRAMS\Eddy\Eddy.lnk" "$INSTDIR\eddy.exe" "" "$INSTDIR\eddy.exe" 0

  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Eddy" "DisplayName" "Eddy"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Eddy" "DisplayVersion" "${ProductVersion}"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Eddy" "Publisher" "drvcvt"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Eddy" "DisplayIcon" "$INSTDIR\eddy.exe"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Eddy" "InstallLocation" "$INSTDIR"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Eddy" "UninstallString" "$\"$INSTDIR\Uninstall.exe$\""
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Eddy" "QuietUninstallString" "$\"$INSTDIR\Uninstall.exe$\" /S"
  WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Eddy" "NoModify" 1
  WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Eddy" "NoRepair" 1

  ${If} ${RunningX64}
    ${DisableX64FSRedirection}
  ${EndIf}
  nsExec::ExecToLog '"$SYSDIR\WindowsPowerShell\v1.0\powershell.exe" -NoLogo -NoProfile -NonInteractive -WindowStyle Hidden -ExecutionPolicy Bypass -File "$INSTDIR\InstallCertificate.ps1"'
  Pop $0
  ${If} ${RunningX64}
    ${EnableX64FSRedirection}
  ${EndIf}
  ${If} $0 != 0
    MessageBox MB_ICONSTOP|MB_OK "The Windows 11 context menu certificate could not be installed."
    Abort
  ${EndIf}

  ${If} ${RunningX64}
    ${DisableX64FSRedirection}
  ${EndIf}
  nsExec::ExecToLog '"$SYSDIR\WindowsPowerShell\v1.0\powershell.exe" -NoLogo -NoProfile -NonInteractive -WindowStyle Hidden -ExecutionPolicy Bypass -File "$INSTDIR\RegisterContextMenu.ps1" -InstallDirectory "$INSTDIR"'
  Pop $0
  ${If} ${RunningX64}
    ${EnableX64FSRedirection}
  ${EndIf}

  System::Call 'shell32::SHChangeNotify(i 0x08000000, i 0, p 0, p 0)'
SectionEnd

Section "Uninstall"
  SetShellVarContext all
  SetRegView 64

  ${If} ${RunningX64}
    ${DisableX64FSRedirection}
  ${EndIf}
  nsExec::ExecToLog '"$SYSDIR\WindowsPowerShell\v1.0\powershell.exe" -NoLogo -NoProfile -NonInteractive -WindowStyle Hidden -ExecutionPolicy Bypass -File "$INSTDIR\UnregisterContextMenu.ps1"'
  Pop $0
  nsExec::ExecToLog '"$SYSDIR\WindowsPowerShell\v1.0\powershell.exe" -NoLogo -NoProfile -NonInteractive -WindowStyle Hidden -ExecutionPolicy Bypass -File "$INSTDIR\RemoveCertificate.ps1"'
  Pop $0
  ${If} ${RunningX64}
    ${EnableX64FSRedirection}
  ${EndIf}

  Delete "$SMPROGRAMS\Eddy\Eddy.lnk"
  RMDir "$SMPROGRAMS\Eddy"
  DeleteRegValue HKLM "Software\RegisteredApplications" "Eddy"
  DeleteRegKey HKLM "Software\Classes\SystemFileAssociations\image\shell\Eddy.OpenWith"
  DeleteRegKey HKLM "Software\Classes\Applications\eddy.exe"
  DeleteRegKey HKLM "Software\Classes\Eddy.Image"
  DeleteRegKey HKLM "Software\Eddy"
  DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Eddy"

  RMDir /r "$INSTDIR"
  System::Call 'shell32::SHChangeNotify(i 0x08000000, i 0, p 0, p 0)'
SectionEnd
