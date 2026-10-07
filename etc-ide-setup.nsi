; ============================================================
;  ETC IDE + ETC Lang 编译器 · NSIS 安装程序
;  v1.3.0  © 2026 ET
; ============================================================
Unicode true
!include "MUI2.nsh"

; ---------- 元数据 ----------
!define APP_NAME "ETC IDE"
!define APP_VERSION "v1.3.0"
!define APP_PUBLISHER "ET"
!define APP_WEBSITE "https://etqwfd.github.io/ETC-IDE/"
!define APP_ICON "assets\etc-ide.ico"

Name "ETC Lang · ETC IDE"
OutFile "out\ETC-IDE-setup-v1.3.0.exe"
InstallDir "$PROGRAMFILES\ETC\ETC-IDE"
RequestExecutionLevel admin
SetCompressor /SOLID lzma

Icon "${APP_ICON}"
UninstallIcon "${APP_ICON}"
VIProductVersion "1.3.0.0"
VIAddVersionKey "ProductName" "ETC Lang · ETC IDE"
VIAddVersionKey "CompanyName" "ET"
VIAddVersionKey "LegalCopyright" "Copyright (c) ET 2026"
VIAddVersionKey "FileDescription" "ETC Lang 编译器 + ETC IDE 集成开发环境"
VIAddVersionKey "FileVersion" "1.3.0.0"

; ---------- 界面 ----------
!define MUI_ABORTWARNING
!define MUI_ICON "${APP_ICON}"
!define MUI_UNICON "${APP_ICON}"

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_LICENSE "LICENSE"
!insertmacro MUI_PAGE_COMPONENTS
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

!insertmacro MUI_LANGUAGE "SimpChinese"

; ---------- 组件 ----------
Section "ETC IDE 主程序（必需）" SecMain
    SectionIn RO
    SetOutPath "$INSTDIR"
    File "build\ETC-IDE.exe"
    File "build\come.exe"
    File "assets\etc-ide.ico"
    File "README.md"
    File "LICENSE"
    File "CHANGELOG.md"

    SetOutPath "$INSTDIR\samples"
    File /r "samples\*.*"

    SetOutPath "$INSTDIR\templates"
    File /r "templates\*.*"

    ; 开始菜单
    CreateDirectory "$SMPROGRAMS\ETC"
    CreateShortcut "$SMPROGRAMS\ETC\ETC IDE.lnk" "$INSTDIR\ETC-IDE.exe" "" "$INSTDIR\etc-ide.ico"
    CreateShortcut "$SMPROGRAMS\ETC\卸载 ETC IDE.lnk" "$INSTDIR\uninstall.exe"

    ; 桌面快捷方式
    CreateShortcut "$DESKTOP\ETC IDE.lnk" "$INSTDIR\ETC-IDE.exe" "" "$INSTDIR\etc-ide.ico"

    ; 卸载信息
    WriteUninstaller "$INSTDIR\uninstall.exe"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\ETC IDE" \
        "DisplayName" "ETC Lang · ETC IDE v1.3.0"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\ETC IDE" \
        "DisplayIcon" "$INSTDIR\etc-ide.ico"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\ETC IDE" \
        "UninstallString" "$INSTDIR\uninstall.exe"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\ETC IDE" \
        "Publisher" "ET"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\ETC IDE" \
        "DisplayVersion" "1.3.0"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\ETC IDE" \
        "InstallLocation" "$INSTDIR"
SectionEnd

Section /o "便携编译器工具链（推荐勾选，约 300MB）" SecToolchain
    SetOutPath "$INSTDIR\toolchain"
    File /r "toolchain\*.*"
SectionEnd

; ---------- 卸载 ----------
Section "Uninstall"
    Delete "$INSTDIR\ETC-IDE.exe"
    Delete "$INSTDIR\come.exe"
    Delete "$INSTDIR\etc-ide.ico"
    Delete "$INSTDIR\README.md"
    Delete "$INSTDIR\LICENSE"
    Delete "$INSTDIR\CHANGELOG.md"
    Delete "$INSTDIR\uninstall.exe"
    RMDir /r "$INSTDIR\samples"
    RMDir /r "$INSTDIR\templates"
    RMDir /r "$INSTDIR\toolchain"
    RMDir "$INSTDIR"

    Delete "$SMPROGRAMS\ETC\ETC IDE.lnk"
    Delete "$SMPROGRAMS\ETC\卸载 ETC IDE.lnk"
    RMDir "$SMPROGRAMS\ETC"
    Delete "$DESKTOP\ETC IDE.lnk"

    DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\ETC IDE"
SectionEnd
