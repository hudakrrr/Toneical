; Toneical - guitar rig
; SPDX-License-Identifier: AGPL-3.0-or-later
;
; Inno Setup 6 script: one Toneical-<version>-Windows-x64-Setup.exe with the standalone app,
; the VST3 plugin, the factory presets and the licences.
;
; Built by packaging\windows\build.ps1 (or the GitHub "build" workflow):
;   ISCC.exe /DAppVersion=1.6.4 /DBuildDir=<build>\Toneical_artefacts\Release toneical.iss

#define AppName "Toneical"
#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif
#ifndef BuildDir
  #define BuildDir "..\..\build-windows\Toneical_artefacts\Release"
#endif
#define Root "..\.."

[Setup]
AppId={{7DD0D0EE-3DED-44FF-A6EE-2E800CF4EE1F}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher=komoj
AppPublisherURL=https://komoj.hu/toneical
DefaultDirName={autopf}\{#AppName}
DefaultGroupName={#AppName}
DisableProgramGroupPage=yes
LicenseFile={#Root}\LICENSE
OutputDir={#Root}\dist
OutputBaseFilename={#AppName}-{#AppVersion}-Windows-x64-Setup
SetupIconFile={#Root}\assets\icon\toneical.ico
UninstallDisplayIcon={app}\Toneical.exe
UninstallDisplayName={#AppName}
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
; the VST3 plugin goes to C:\Program Files\Common Files\VST3, which needs admin rights
PrivilegesRequired=admin
Compression=lzma2/ultra64
SolidCompression=yes
WizardStyle=modern
CloseApplications=yes

[Tasks]
Name: "vst3"; Description: "Install the VST3 plugin (use Toneical inside a DAW: Reaper, Cubase, Ableton, ...)"; GroupDescription: "Plugin:"
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"

[Files]
Source: "{#BuildDir}\Standalone\Toneical.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#BuildDir}\VST3\Toneical.vst3\*"; DestDir: "{commoncf64}\VST3\Toneical.vst3"; Excludes: "desktop.ini"; Flags: ignoreversion recursesubdirs createallsubdirs; Tasks: vst3
Source: "{#BuildDir}\VST3\Toneical.vst3\desktop.ini"; DestDir: "{commoncf64}\VST3\Toneical.vst3"; Attribs: hidden system; Flags: ignoreversion skipifsourcedoesntexist; Tasks: vst3
; factory presets as a data file: found by the app and the plugin (see Presets.cpp)
Source: "{#Root}\presets\factory.json"; DestDir: "{commonappdata}\Toneical\Factory"; Flags: ignoreversion
Source: "{#Root}\README.md"; DestDir: "{app}\docs"; Flags: ignoreversion
Source: "{#Root}\LICENSE"; DestDir: "{app}\licenses"; DestName: "LICENSE-Toneical-AGPL-3.0.txt"; Flags: ignoreversion
Source: "{#Root}\THIRD_PARTY.md"; DestDir: "{app}\licenses"; Flags: ignoreversion
Source: "{#Root}\assets\irs\LICENSE-BestPlugins.txt"; DestDir: "{app}\licenses\irs"; Flags: ignoreversion
Source: "{#Root}\assets\irs\GPL-2.txt"; DestDir: "{app}\licenses\irs"; Flags: ignoreversion
Source: "{#Root}\assets\irs\README.txt"; DestDir: "{app}\licenses\irs"; Flags: ignoreversion
Source: "{#Root}\assets\nam\COPYING-GPL-3.txt"; DestDir: "{app}\licenses\nam"; Flags: ignoreversion
Source: "{#Root}\assets\nam\README.txt"; DestDir: "{app}\licenses\nam"; Flags: ignoreversion
Source: "{#Root}\assets\pedals\LICENSE-ToneLibrary-GPL-3.txt"; DestDir: "{app}\licenses\pedals"; Flags: ignoreversion
Source: "{#Root}\assets\pedals\README.txt"; DestDir: "{app}\licenses\pedals"; Flags: ignoreversion
; optional: drop lame.exe into packaging\windows\extra\ to ship MP3 export ready to use
#if FileExists(AddBackslash(SourcePath) + "extra\lame.exe")
Source: "extra\lame.exe"; DestDir: "{app}"; Flags: ignoreversion
#endif

[Icons]
Name: "{autoprograms}\{#AppName}"; Filename: "{app}\Toneical.exe"
Name: "{autoprograms}\{#AppName} - your presets, captures and projects"; Filename: "{userdocs}\Toneical"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\Toneical.exe"; Tasks: desktopicon

[Dirs]
; the plugin bundle shows its icon in Explorer (desktop.ini) only when the folder is a "system" folder
Name: "{commoncf64}\VST3\Toneical.vst3"; Attribs: system; Tasks: vst3
; where your own files go (the app also creates them when needed)
Name: "{userdocs}\Toneical\Presets"; Flags: uninsneveruninstall
Name: "{userdocs}\Toneical\Captures"; Flags: uninsneveruninstall
Name: "{userdocs}\Toneical\IRs"; Flags: uninsneveruninstall
Name: "{userdocs}\Toneical\Projects"; Flags: uninsneveruninstall

[Run]
Filename: "{app}\Toneical.exe"; Description: "{cm:LaunchProgram,{#AppName}}"; Flags: nowait postinstall skipifsilent

[UninstallDelete]
Type: filesandordirs; Name: "{commonappdata}\Toneical\Factory"
