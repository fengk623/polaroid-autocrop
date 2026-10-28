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
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
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
#ifexist "compiler:Languages\ChineseSimplified.isl"
Name: "zh"; MessagesFile: "compiler:Languages\ChineseSimplified.isl"
#endif

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked
Name: "assocproj"; Description: "Open .fkproj project files with FrameKeeper"; GroupDescription: "File associations:"

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
