; Loki Metadata installer (Inno Setup 6). Built by scripts/package.ps1, which
; passes the version and the folder the release was laid out in:
;   ISCC /DMyAppVersion=0.1.1 "/DSourceDir=..\build\release\Loki Metadata" installer\LokiMetadata.iss

#ifndef MyAppVersion
  #define MyAppVersion "0.1.1"
#endif
#ifndef SourceDir
  #define SourceDir "..\build\release\Loki Metadata"
#endif

[Setup]
AppId={{089428F9-AA99-4113-B22A-05BD17FC1B2C}
AppName=Loki Metadata
AppVersion={#MyAppVersion}
AppVerName=Loki Metadata {#MyAppVersion}
AppPublisher=Chaos Rift Entertainment, LLC
AppPublisherURL=https://github.com/Deverydoo/Loki-Metadata
AppSupportURL=https://github.com/Deverydoo/Loki-Metadata/issues
AppUpdatesURL=https://github.com/Deverydoo/Loki-Metadata/releases
AppCopyright=Copyright 2026 Chaos Rift Entertainment, LLC
VersionInfoVersion={#MyAppVersion}
VersionInfoCompany=Chaos Rift Entertainment, LLC
VersionInfoDescription=Loki Metadata installer
DefaultDirName={autopf}\Loki Metadata
DisableProgramGroupPage=yes
LicenseFile=..\LICENSE
OutputDir=..\dist
OutputBaseFilename=LokiMetadata-{#MyAppVersion}-Setup
SetupIconFile=..\app\resources\LokiMetadata.ico
UninstallDisplayIcon={app}\LokiMetadata.exe
UninstallDisplayName=Loki Metadata {#MyAppVersion}
Compression=lzma2/ultra64
SolidCompression=yes
WizardStyle=modern
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
; Installs for everyone by default; the person installing can choose just themselves.
PrivilegesRequired=admin
PrivilegesRequiredOverridesAllowed=dialog
CloseApplications=yes

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
Source: "{#SourceDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs; Excludes: "*.old"

[Icons]
Name: "{autoprograms}\Loki Metadata"; Filename: "{app}\LokiMetadata.exe"
Name: "{autodesktop}\Loki Metadata"; Filename: "{app}\LokiMetadata.exe"; Tasks: desktopicon

[Run]
Filename: "{app}\LokiMetadata.exe"; Description: "{cm:LaunchProgram,Loki Metadata}"; Flags: nowait postinstall skipifsilent
