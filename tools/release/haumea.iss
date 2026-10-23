; SPDX-License-Identifier: GPL-2.0-or-later
; ISCC /DBundleDir=... /DOutputDir=... /DAppVersion=1.0.0.1 haumea.iss
[Setup]
AppId={{365016D7-6E2F-45C9-9A65-8FBBDA826031}
AppName=Haumea Studio
AppVersion={#AppVersion}
AppPublisher=Haumea Studio
DefaultDirName={localappdata}\Programs\Haumea Studio
DefaultGroupName=Haumea Studio
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
OutputDir={#OutputDir}
OutputBaseFilename=Haumea-Studio-Windows-Setup
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
LicenseFile={#BundleDir}\LICENSE
UninstallDisplayIcon={app}\haumea.exe
CloseApplications=yes
DisableProgramGroupPage=yes

[Tasks]
Name: "desktopicon"; Description: "Create a desktop shortcut"; Flags: unchecked

[Files]
Source: "{#BundleDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{userprograms}\Haumea Studio"; Filename: "{app}\haumea.exe"; WorkingDir: "{app}"
Name: "{userdesktop}\Haumea Studio"; Filename: "{app}\haumea.exe"; WorkingDir: "{app}"; Tasks: desktopicon

[Run]
Filename: "{app}\haumea.exe"; Description: "Start Haumea Studio"; WorkingDir: "{app}"; Flags: nowait postinstall skipifsilent
