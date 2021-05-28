; Script generated by the HM NIS Edit Script Wizard.

; HM NIS Edit Wizard helper defines

Unicode True

!addplugindir ./deployment

!ifndef PRODUCT_VERSION
!warning "VERSION not defined!"
!define PRODUCT_VERSION "0.0.0.0"
!endif

!ifndef PRODUCT_ARCH
!warning "ARCH not defined!"
!define PRODUCT_ARCH "unknown"
!endif
 
!ifndef BASE_DIR
!warning "BASE_DIR not defined!"
!define BASE_DIR "."
!endif

!define PRODUCT_NAME "DDCToolbox"
!define PRODUCT_PUBLISHER "ThePBone"
!define PRODUCT_WEB_SITE "https://github.com/ThePBone/DDCToolbox"
!define PRODUCT_DIR_REGKEY "Software\Microsoft\Windows\CurrentVersion\App Paths\DDCToolbox.exe"
!define PRODUCT_UNINST_KEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\${PRODUCT_NAME}"
!define PRODUCT_UNINST_ROOT_KEY "HKLM"

; MUI 1.67 compatible ------
!include "MUI.nsh"

; MUI Settings
!define MUI_ABORTWARNING
!define MUI_ICON "${NSISDIR}\Contrib\Graphics\Icons\modern-install.ico"
!define MUI_UNICON "${NSISDIR}\Contrib\Graphics\Icons\modern-uninstall.ico"

; Welcome page
!insertmacro MUI_PAGE_WELCOME
; License page
!define MUI_LICENSEPAGE_CHECKBOX
!insertmacro MUI_PAGE_LICENSE "LICENSE"
; Directory page
!insertmacro MUI_PAGE_DIRECTORY
; Instfiles page
!insertmacro MUI_PAGE_INSTFILES
; Finish page
!define MUI_FINISHPAGE_RUN "$INSTDIR\DDCToolbox.exe"
!insertmacro MUI_PAGE_FINISH

; Uninstaller pages
!insertmacro MUI_UNPAGE_INSTFILES

; Language files
!insertmacro MUI_LANGUAGE "English"

; MUI end ------

!include WinMessages.nsh

; !defines for use with SHChangeNotify
!ifdef SHCNE_ASSOCCHANGED
!undef SHCNE_ASSOCCHANGED
!endif
!define SHCNE_ASSOCCHANGED 0x08000000
!ifdef SHCNF_FLUSH
!undef SHCNF_FLUSH
!endif
!define SHCNF_FLUSH        0x1000
 
!macro UPDATEFILEASSOC
; Using the system.dll plugin to call the SHChangeNotify Win32 API function so we
; can update the shell.
  System::Call "shell32::SHChangeNotify(i,i,i,i) (${SHCNE_ASSOCCHANGED}, ${SHCNF_FLUSH}, 0, 0)"
!macroend

!define WND_CLASS "Qt5152QWindowIcon"
!define WND_TITLE "DDC Toolbox"
!define TO_MS 2000
!define SYNC_TERM 0x00100001

LangString termMsg ${LANG_ENGLISH} "${WND_TITLE} is still running and cannot be closed gracefully.$\nDo you want to forcefully exit the application to continue?"
LangString stopMsg ${LANG_ENGLISH} "Stopping ${WND_TITLE} Application"
 
!macro TerminateApp
	Push $0 ; window handle
	Push $1
	Push $2 ; process handle
	FindProcDLL::FindProc "DDCToolbox.exe"
	IntCmp $0 1 done
	DetailPrint "$(stopMsg)"
	FindWindow $0 '${WND_CLASS}' ''
	IntCmp $0 0 done
	System::Call 'user32.dll::GetWindowThreadProcessId(i r0, *i .r1) i .r2'
	System::Call 'kernel32.dll::OpenProcess(i ${SYNC_TERM}, i 0, i r1) i .r2'
	SendMessage $0 ${WM_CLOSE} 0 0 /TIMEOUT=${TO_MS}
	System::Call 'kernel32.dll::WaitForSingleObject(i r2, i ${TO_MS}) i .r1'
	IntCmp $1 0 close
	System::Call 'kernel32.dll::TerminateProcess(i r2, i 0) i .r1'
	FindProcDLL::WaitProcEnd "DDCToolbox.exe" 500
	Sleep 150
  close:
	System::Call 'kernel32.dll::CloseHandle(i r2) i .r1'
  done:
	Pop $2
	Pop $1
	Pop $0
!macroend

Name "${PRODUCT_NAME} ${PRODUCT_VERSION}"
OutFile "DDCToolbox_Setup_${PRODUCT_ARCH}_${PRODUCT_VERSION}.exe"
InstallDir "$PROGRAMFILES\DDCToolbox"
InstallDirRegKey HKLM "${PRODUCT_DIR_REGKEY}" ""
ShowInstDetails show
ShowUnInstDetails show

Section "Hauptgruppe" SEC01
  !insertmacro TerminateApp

  SetOutPath "$INSTDIR"
  SetOverwrite on
  File "${BASE_DIR}\DDCToolbox.exe"
  CreateDirectory "$SMPROGRAMS\DDCToolbox"
  CreateShortCut "$SMPROGRAMS\DDCToolbox\DDCToolbox.lnk" "$INSTDIR\DDCToolbox.exe"
  CreateShortCut "$DESKTOP\DDCToolbox.lnk" "$INSTDIR\DDCToolbox.exe"
  
  WriteRegStr HKCR ".vdcprj" "" "DDCToolbox.Project"
  WriteRegStr HKCR "DDCToolbox.Project" "" \
      "Viper DDC Project"
  WriteRegStr HKCR "DDCToolbox.Project\DefaultIcon" "" \
      "$INSTDIR\DDCToolbox.exe,0"
  WriteRegStr HKCR "DDCToolbox.Project\shell\open\command" "" \
      '"$INSTDIR\DDCToolbox.exe" "%1"'
  !insertmacro UPDATEFILEASSOC
SectionEnd

Section -AdditionalIcons
  WriteIniStr "$INSTDIR\${PRODUCT_NAME}.url" "InternetShortcut" "URL" "${PRODUCT_WEB_SITE}"
SectionEnd

Section -Post
  WriteUninstaller "$INSTDIR\uninst.exe"
  WriteRegStr HKLM "${PRODUCT_DIR_REGKEY}" "" "$INSTDIR\DDCToolbox.exe"
  WriteRegStr ${PRODUCT_UNINST_ROOT_KEY} "${PRODUCT_UNINST_KEY}" "DisplayName" "$(^Name) (${PRODUCT_ARCH})"
  WriteRegStr ${PRODUCT_UNINST_ROOT_KEY} "${PRODUCT_UNINST_KEY}" "UninstallString" "$INSTDIR\uninst.exe"
  WriteRegStr ${PRODUCT_UNINST_ROOT_KEY} "${PRODUCT_UNINST_KEY}" "DisplayIcon" "$INSTDIR\DDCToolbox.exe"
  WriteRegStr ${PRODUCT_UNINST_ROOT_KEY} "${PRODUCT_UNINST_KEY}" "DisplayVersion" "${PRODUCT_VERSION}"
  WriteRegStr ${PRODUCT_UNINST_ROOT_KEY} "${PRODUCT_UNINST_KEY}" "URLInfoAbout" "${PRODUCT_WEB_SITE}"
  WriteRegStr ${PRODUCT_UNINST_ROOT_KEY} "${PRODUCT_UNINST_KEY}" "Publisher" "${PRODUCT_PUBLISHER}"
SectionEnd


Function un.onUninstSuccess
  HideWindow
  MessageBox MB_ICONINFORMATION|MB_OK "$(^Name) has been successfully uninstalled."
FunctionEnd

Function un.onInit
  MessageBox MB_ICONQUESTION|MB_YESNO|MB_DEFBUTTON2 "Do you want to uninstall $(^Name) and all its components?" IDYES +2
  Abort
FunctionEnd

Section Uninstall
  !insertmacro TerminateApp

  Delete "$INSTDIR\${PRODUCT_NAME}.url"
  Delete "$INSTDIR\uninst.exe"
  Delete "$INSTDIR\DDCToolbox.exe"

  Delete "$DESKTOP\DDCToolbox.lnk"
  Delete "$SMPROGRAMS\DDCToolbox\DDCToolbox.lnk"

  RMDir "$SMPROGRAMS\DDCToolbox"
  RMDir "$INSTDIR"

  DeleteRegKey ${PRODUCT_UNINST_ROOT_KEY} "${PRODUCT_UNINST_KEY}"
  DeleteRegKey HKLM "${PRODUCT_DIR_REGKEY}"
  DeleteRegKey HKCR ".vdcprj" 
  
  !insertmacro UPDATEFILEASSOC
  SetAutoClose true
SectionEnd
