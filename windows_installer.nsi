Name "Gryph"
OutFile "GryphSetup.exe"

; 1. NEVER ask for UAC on launch
RequestExecutionLevel user 

SetCompressor /SOLID /FINAL lzma
SetCompressorDictSize 64

!include MUI2.nsh
!include nsDialogs.nsh
!include LogicLib.nsh
!include FileFunc.nsh
!include WinVer.nsh
!include x64.nsh

!define MUI_ICON "res\Gryph.ico"
!define MUI_ABORTWARNING
!define MUI_WELCOMEPAGE_TITLE "Welcome to Gryph Installer"
!define MUI_WELCOMEPAGE_TEXT "This wizard will guide you through the installation of Gryph."
!define MUI_FINISHPAGE_RUN "$INSTDIR\Gryph.exe"
!define MUI_FINISHPAGE_RUN_TEXT "Launch Gryph"
!addplugindir .\script\

; This is the Windows constant used to draw the UAC Shield on a button
!ifndef BCM_SETSHIELD
  !define BCM_SETSHIELD 0x160C
!endif

Var RadioAllUsers
Var RadioJustMe
Var IsAllUsers
Var SkipPages
Var UninstPath

; =====================================
; PAGE SEQUENCE
; =====================================
!define MUI_PAGE_CUSTOMFUNCTION_PRE SkipPageCheck
!insertmacro MUI_PAGE_WELCOME

; Custom pages do not use MUI_PAGE_CUSTOMFUNCTION_PRE.
; The skip logic is handled directly inside InstallModePageCreate.
Page custom InstallModePageCreate InstallModePageLeave

!define MUI_PAGE_CUSTOMFUNCTION_PRE SkipPageCheck
!define MUI_PAGE_CUSTOMFUNCTION_SHOW DirectoryShow
!define MUI_PAGE_CUSTOMFUNCTION_LEAVE DirectoryLeave
!insertmacro MUI_PAGE_DIRECTORY

!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH

; Uninstaller pages
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

!insertmacro MUI_LANGUAGE "English"

UninstallText "This will uninstall Gryph. Do you wish to continue?"
UninstallIcon "res\GryphDel.ico"

; =====================================
; INIT & SEAMLESS RESTART LOGIC
; =====================================
Function .onInit
  ; Check if this is the elevated restart
  ${GetParameters} $R0
  ${GetOptions} $R0 "/ELEVATED" $R1

  ${IfNot} ${Errors}
    ; We are the elevated process! Skip the UI pages and set context to All Users
    StrCpy $SkipPages 1
    StrCpy $IsAllUsers "1"

    ; Read the chosen installation path from the temporary registry key
    ReadRegStr $INSTDIR HKCU "Software\Gryph" "TempSetupPath"
    DeleteRegValue HKCU "Software\Gryph" "TempSetupPath" ; Clean it up immediately

    SetShellVarContext all
  ${Else}
    ; Normal launch. Default to "Just Me" (Current User)
    StrCpy $IsAllUsers "0"
    StrCpy $INSTDIR "$LOCALAPPDATA\Gryph"
    SetShellVarContext current
  ${EndIf}
FunctionEnd

Function SkipPageCheck
  ${If} $SkipPages == 1
    Abort ; Skipping this page because we are already elevated and ready to install
  ${EndIf}
FunctionEnd

; =====================================
; CUSTOM JUST ME / ALL USERS PAGE
; =====================================
Function InstallModePageCreate
  ${If} $SkipPages == 1
    Abort ; Skip this page if elevated
  ${EndIf}

  !insertmacro MUI_HEADER_TEXT "Installation Options" "Choose who can use this application."
  nsDialogs::Create 1018
  Pop $0

  ${NSD_CreateLabel} 0 0 100% 24u "Please select whether you wish to make this software available to all users or just yourself."
  Pop $0

  ${NSD_CreateRadioButton} 10u 30u 100% 12u "Install for anyone using this computer"
  Pop $RadioAllUsers

  ${NSD_CreateRadioButton} 10u 50u 100% 12u "Install just for me"
  Pop $RadioJustMe

  ${If} $IsAllUsers == "1"
    SendMessage $RadioAllUsers ${BM_SETCHECK} ${BST_CHECKED} 0
  ${Else}
    SendMessage $RadioJustMe ${BM_SETCHECK} ${BST_CHECKED} 0
  ${EndIf}

  nsDialogs::Show
FunctionEnd

Function InstallModePageLeave
  ${NSD_GetState} $RadioAllUsers $0
  ${If} $0 == ${BST_CHECKED}
    StrCpy $IsAllUsers "1"
    StrCpy $INSTDIR "$PROGRAMFILES64\Gryph"
  ${Else}
    StrCpy $IsAllUsers "0"
    StrCpy $INSTDIR "$LOCALAPPDATA\Gryph"
  ${EndIf}
FunctionEnd

; =====================================
; DIRECTORY PAGE & UAC SHIELD
; =====================================
Function DirectoryShow
  ; Target the "Next" (Install) button
  GetDlgItem $0 $HWNDPARENT 1

  ${If} $IsAllUsers == "1"
    SendMessage $0 ${BCM_SETSHIELD} 0 1 ; Draw Shield
  ${Else}
    SendMessage $0 ${BCM_SETSHIELD} 0 0 ; Remove Shield
  ${EndIf}
FunctionEnd

Function DirectoryLeave
  ${If} $IsAllUsers == "1"
    ; --- ALL USERS LOGIC ---
    UserInfo::GetAccountType
    Pop $0
    ${If} $0 != "Admin"
      ; Write the chosen path safely to the registry for the elevated process to grab
      WriteRegStr HKCU "Software\Gryph" "TempSetupPath" "$INSTDIR"

      ; Trigger UAC and silently launch the elevated installer
      ExecShell "runas" "$EXEPATH" "/ELEVATED"
      Quit ; Close the unelevated window
    ${EndIf}
    SetShellVarContext all

  ${Else}
    ; --- JUST ME LOGIC ---
    ; Verify they aren't trying to install to an Admin folder without Admin rights
    ClearErrors
    CreateDirectory "$INSTDIR"
    IfErrors AccessDenied

    GetTempFileName $0 "$INSTDIR"
    FileOpen $1 $0 w
    FileWrite $1 "test"
    FileClose $1
    IfErrors AccessDenied

    Delete $0
    SetShellVarContext current
    Goto DoneCheck

    AccessDenied:
      MessageBox MB_OK|MB_ICONSTOP "You do not have permission to install to '$INSTDIR'.$\n$\nPlease choose a different folder, or select 'Install for anyone using this computer'."
      Abort ; Forces them to stay on the Directory page

    DoneCheck:
  ${EndIf}
FunctionEnd

!macro AbortOnRunningApp EXEName
  killModule:
  FindProcDLL::FindProc ${EXEName}
  Pop $R0
  IntCmp $R0 1 0 notRunning
    FindProcDLL::KillProc ${EXEName}
    Sleep 1000
    Goto killModule
  notRunning:
!macroend

; =====================================
; INSTALLATION
; =====================================
Section "Install"
  SetOutPath "$INSTDIR"
  SetOverwrite on

  !insertmacro AbortOnRunningApp "$INSTDIR\Gryph.exe"

  ${If} ${IsNativeAMD64}
  ${If} ${AtLeastWaaS} 1809
    File /r /x "*.pdb" /x "*.lib" /x "*.exp" "dist\windows-amd64\*.*"
  ${Else}
      ;File /oname=GryphCore.exe "deployment\windowslegacy-amd64\GryphCore.exe"
      ;File /oname=Gryph.exe "deployment\windowslegacy-amd64\Gryph.exe"
      ;File /oname=updater.exe "deployment\windowslegacy-amd64\updater.exe"
    ${EndIf}
  ${ElseIf} ${IsNativeARM64}
    ;File /oname=libcronet.dll "deployment\windows-arm64\libcronet.dll"
    ;File /oname=GryphCore.exe "deployment\windows-arm64\GryphCore.exe"
    ;File /oname=Gryph.exe "deployment\windows-arm64\Gryph.exe"
    ;File /oname=updater.exe "deployment\windows-arm64\updater.exe"
  ${ElseIf} ${IsNativeIA32}
    ;File /oname=GryphCore.exe "deployment\windowslegacy-386\GryphCore.exe"
    ;File /oname=Gryph.exe "deployment\windowslegacy-386\Gryph.exe"
    ;File /oname=updater.exe "deployment\windowslegacy-386\updater.exe"
  ${Else}
    Abort "Unsupported CPU architecture!"
  ${EndIf}

  CreateShortcut "$DESKTOP\Gryph.lnk" "$INSTDIR\Gryph.exe"
  CreateShortcut "$SMPROGRAMS\Gryph.lnk" "$INSTDIR\Gryph.exe" "" "$INSTDIR\Gryph.exe" 0

  WriteRegStr SHCTX "Software\Gryph" "InstallPath" "$INSTDIR"
  WriteRegStr SHCTX "Software\Microsoft\Windows\CurrentVersion\Uninstall\Gryph" "DisplayName" "Gryph"
  WriteRegStr SHCTX "Software\Microsoft\Windows\CurrentVersion\Uninstall\Gryph" "UninstallString" "$INSTDIR\uninstall.exe"
  WriteRegStr SHCTX "Software\Microsoft\Windows\CurrentVersion\Uninstall\Gryph" "InstallLocation" "$INSTDIR"
  WriteRegDWORD SHCTX "Software\Microsoft\Windows\CurrentVersion\Uninstall\Gryph" "NoModify" 1
  WriteRegDWORD SHCTX "Software\Microsoft\Windows\CurrentVersion\Uninstall\Gryph" "NoRepair" 1
  WriteUninstaller "uninstall.exe"
SectionEnd

; =====================================
; UNINSTALLER
; =====================================
Function un.onInit
  ClearErrors

  ; Grab the folder passed via command-line (if we elevated), otherwise use standard folder
  ${GetParameters} $R0
  ${GetOptions} $R0 "/UINSTDIR=" $R1

  ${If} $R1 != ""
    StrCpy $UninstPath $R1
  ${Else}
    StrCpy $UninstPath $INSTDIR
  ${EndIf}

  ; Read the Admin registry to see if THIS specific folder belongs to an Admin installation
  ReadRegStr $0 HKLM "Software\Gryph" "InstallPath"

  ${If} $0 == $UninstPath
    ; --- IT IS AN ALL USERS INSTALL ---
    SetShellVarContext all
    UserInfo::GetAccountType
    Pop $1
    ${If} $1 != "Admin"
       MessageBox MB_YESNO|MB_ICONEXCLAMATION "Uninstalling Gryph for all users requires Administrator privileges.$\n$\nDo you want to elevate?" IDNO Stay
       ; Elevate via UAC and explicitly pass the real folder path in quotes!
       ExecShell "runas" "$EXEPATH" '/UINSTDIR="$UninstPath"'
       Quit
    Stay:
       Abort
    ${EndIf}
  ${Else}
    ; --- IT IS A JUST ME INSTALL (OR ORPHANED) ---
    SetShellVarContext current
  ${EndIf}

  ; Force $INSTDIR to the true path so %TEMP% executions don't delete themselves!
  StrCpy $INSTDIR $UninstPath
FunctionEnd

Section "Uninstall"
  !insertmacro AbortOnRunningApp "$INSTDIR\Gryph.exe"

  Delete "$SMPROGRAMS\Gryph.lnk"
  Delete "$DESKTOP\Gryph.lnk"
  RMDir "$SMPROGRAMS\Gryph"

  RMDir /r "$INSTDIR"

  ; Clean up registry!
  DeleteRegKey SHCTX "Software\Microsoft\Windows\CurrentVersion\Uninstall\Gryph"
  DeleteRegKey SHCTX "Software\Gryph"
SectionEnd
