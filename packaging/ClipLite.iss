#define AppName "ClipLite"
#ifndef AppVersion
  #define AppVersion "0.1.0"
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
UninstallDisplayIcon={app}\ClipLite.exe
ChangesAssociations=no

[Tasks]
Name: "desktopicon"; Description: "Create a desktop shortcut"; GroupDescription: "Additional shortcuts:"

[Files]
Source: "{#SourceRoot}\build-x64\Release\ClipLite.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#SourceRoot}\README.md"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#SourceRoot}\CHANGELOG.md"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#SourceRoot}\resources\support-wechat.png"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#SourceRoot}\resources\support-alipay.png"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#SourceRoot}\resources\support-qq.jpg"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{autoprograms}\{#AppName}"; Filename: "{app}\ClipLite.exe"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\ClipLite.exe"; Tasks: desktopicon

[Run]
Filename: "{app}\ClipLite.exe"; Description: "Launch {#AppName}"; Flags: nowait postinstall skipifsilent

[Registry]
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; ValueType: string; ValueName: "ClipLite"; Flags: uninsdeletevalue

[UninstallRun]
Filename: "{app}\ClipLite.exe"; Parameters: "--exit"; RunOnceId: "ClipLiteExit"; Flags: runhidden waituntilterminated
