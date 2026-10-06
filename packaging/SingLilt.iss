; Per-user installation, shortcuts, and uninstall behavior.
; Copyright (c) 2026 Herbert Yeung
; Author: Herbert Yeung
; SPDX-License-Identifier: MIT

#ifndef PayloadDir
  #error PayloadDir must name a verified portable package
#endif

[Setup]
AppId={{A3C44ECB-9947-4F6E-9414-53054F73B742}
AppName=SingLilt
AppVersion={#AppVersion}
AppPublisher=Herbert Yeung
AppPublisherURL=https://github.com/herbertyeung/SingLilt
DefaultDirName={localappdata}\Programs\SingLilt
DefaultGroupName=SingLilt
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0.17763
OutputDir={#OutputDir}
OutputBaseFilename=SingLilt-{#AppVersion}-win-x64-{#Edition}-setup
SetupIconFile={#PayloadDir}\SingLilt.ico
UninstallDisplayIcon={app}\SingLilt.exe
LicenseFile={#PayloadDir}\LICENSE
Compression=lzma2/fast
SolidCompression=yes
LZMANumBlockThreads=2
WizardStyle=modern
CloseApplications=yes
RestartApplications=no

[Tasks]
Name: "desktopicon"; Description: "Create a desktop shortcut"; Flags: unchecked

[Files]
Source: "{#PayloadDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\SingLilt"; Filename: "{app}\SingLilt.exe"
Name: "{userdesktop}\SingLilt"; Filename: "{app}\SingLilt.exe"; Tasks: desktopicon

[Run]
Filename: "{app}\SingLilt.exe"; Description: "Launch SingLilt"; Flags: nowait postinstall skipifsilent
