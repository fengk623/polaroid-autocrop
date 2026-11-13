; FrameKeeper installer (Inno Setup 6). Built by tools/release/package-windows.sh; not signed.
#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif
[Setup]
AppId={{6A4E6C2B-1F0B-4C51-9D7A-3B2E8F4D9C11}
AppName=FrameKeeper
AppVersion={#AppVersion}
AppPublisher=FrameKeeper contributors
AppPublisherURL=https://github.com/fengk623/polaroid-autocrop
AppSupportURL=https://github.com/fengk623/polaroid-autocrop/issues
DefaultDirName={autopf}\FrameKeeper
DefaultGroupName=FrameKeeper
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
; "x64" (not "x64compatible") so that any Inno Setup 6 release compiles this script
ArchitecturesAllowed=x64
ArchitecturesInstallIn64BitMode=x64
OutputDir={#OutputDir}
OutputBaseFilename=FrameKeeper-{#AppVersion}-Windows-x64-Setup
SetupIconFile={#RepoRoot}\app\resources\FrameKeeper.ico
UninstallDisplayIcon={app}\FrameKeeper.exe
LicenseFile={#RepoRoot}\LICENSE
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
ChangesAssociations=yes
MinVersion=10.0

[Languages]
Name: "en"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked
Name: "assocproj"; Description: "Open .fkproj project files with FrameKeeper"; GroupDescription: "File associations:"

[InstallDelete]
; FK-014: an upgrade (or a rollback) replaces Qt's plugin and QML folders and the licence texts whole, so no
; file of the other version is left there (Qt loads every plugin it finds). The user's files never live in the
; program folder (settings, templates and recovery files are under %APPDATA%\FrameKeeper; projects where the
; user saves them).
Type: filesandordirs; Name: "{app}\qml"
Type: filesandordirs; Name: "{app}\platforms"
Type: filesandordirs; Name: "{app}\imageformats"
Type: filesandordirs; Name: "{app}\iconengines"
Type: filesandordirs; Name: "{app}\styles"
Type: filesandordirs; Name: "{app}\generic"
Type: filesandordirs; Name: "{app}\networkinformation"
Type: filesandordirs; Name: "{app}\tls"
Type: filesandordirs; Name: "{app}\qmltooling"
Type: filesandordirs; Name: "{app}\licenses"

[Files]
Source: "{#SourceDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{autoprograms}\FrameKeeper"; Filename: "{app}\FrameKeeper.exe"
Name: "{autodesktop}\FrameKeeper"; Filename: "{app}\FrameKeeper.exe"; Tasks: desktopicon

[Registry]
Root: HKA; Subkey: "Software\Classes\.fkproj"; ValueType: string; ValueName: ""; ValueData: "FrameKeeper.Project"; Flags: uninsdeletevalue; Tasks: assocproj
Root: HKA; Subkey: "Software\Classes\FrameKeeper.Project"; ValueType: string; ValueName: ""; ValueData: "FrameKeeper Project"; Flags: uninsdeletekey; Tasks: assocproj
Root: HKA; Subkey: "Software\Classes\FrameKeeper.Project\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: "{app}\FrameKeeper.exe,0"; Tasks: assocproj
Root: HKA; Subkey: "Software\Classes\FrameKeeper.Project\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\FrameKeeper.exe"" ""%1"""; Tasks: assocproj

[Run]
Filename: "{app}\FrameKeeper.exe"; Description: "{cm:LaunchProgram,FrameKeeper}"; Flags: nowait postinstall skipifsilent
