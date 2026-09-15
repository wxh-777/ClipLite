#define AppName "ClipLite"
#ifndef AppVersion
  #define AppVersion "1.2.4"
#endif
#ifndef SourceRoot
  #define SourceRoot ".."
#endif
#ifndef OutputDir
  #define OutputDir "..\\out"
#endif

[Setup]
AppId={{B4C9B1D1-8A4A-4CFD-9D5A-5E4E1A1B0001}
AppName={#AppName}
AppVersion={#AppVersion}
AppPublisher=ClipLite
DefaultDirName={localappdata}\Programs\ClipLite
DefaultGroupName={#AppName}
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
OutputDir={#OutputDir}
OutputBaseFilename=ClipLite-Setup-{#AppVersion}-x64
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
LanguageDetectionMethod=uilanguage
MissingMessagesWarning=no
UninstallDisplayIcon={app}\ClipLite.exe
ChangesAssociations=no

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"; LicenseFile: "{#SourceRoot}\LICENSE.md"
Name: "chinesesimplified"; MessagesFile: "{#SourceRoot}\packaging\ChineseSimplified.isl"; LicenseFile: "{#SourceRoot}\LICENSE.zh-CN.md"

[CustomMessages]
english.CreateDesktopShortcut=Create a desktop shortcut
chinesesimplified.CreateDesktopShortcut=创建桌面快捷方式
english.AdditionalShortcuts=Additional shortcuts
chinesesimplified.AdditionalShortcuts=其他快捷方式
english.LaunchApp=Launch {#AppName}
chinesesimplified.LaunchApp=启动 {#AppName}

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopShortcut}"; GroupDescription: "{cm:AdditionalShortcuts}:"

[Files]
Source: "{#SourceRoot}\build-x64\Release\ClipLite.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#SourceRoot}\README.md"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#SourceRoot}\README.en.md"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#SourceRoot}\USAGE.md"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#SourceRoot}\CHANGELOG.md"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#SourceRoot}\CHANGELOG.en.md"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#SourceRoot}\LICENSE.md"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#SourceRoot}\LICENSE.zh-CN.md"; DestDir: "{app}"; Flags: ignoreversion
[Icons]
Name: "{autoprograms}\{#AppName}"; Filename: "{app}\ClipLite.exe"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\ClipLite.exe"; Tasks: desktopicon

[Run]
Filename: "{app}\ClipLite.exe"; Description: "{cm:LaunchApp}"; Flags: nowait postinstall skipifsilent

[Registry]
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; ValueType: string; ValueName: "ClipLite"; Flags: uninsdeletevalue

[UninstallRun]
Filename: "{app}\ClipLite.exe"; Parameters: "--exit"; RunOnceId: "ClipLiteExit"; Flags: runhidden waituntilterminated
