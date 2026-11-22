; Build Tabor8200-Control.exe and deploy its Qt runtime with build.ps1 before compiling.
; The NI-488.2 driver is installed separately.
#define ReadQuoted(Text) Copy(Text, Pos('"', Text) + 1, Pos('"', Copy(Text, Pos('"', Text) + 1, Len(Text))) - 1)
#define VersionHeader FileOpen(SourcePath + "version.h")
#expr FileRead(VersionHeader)
#expr FileRead(VersionHeader)
#define Tabor8200Version ReadQuoted(FileRead(VersionHeader))
#expr FileClose(VersionHeader)
#define IconResource FileOpen(SourcePath + "assets\app.rc")
#define Tabor8200Icon ReadQuoted(FileRead(IconResource))
#expr FileClose(IconResource)
#ifndef BuildArch
  #define BuildArch "x64"
#endif
#if BuildArch == "x86"
  #define Tabor8200Name "Tabor 8200 Control (32 Bit)"
  #define DeviceAppId "{{01398E81-3059-5875-B08A-9D700FE34B5B}"
  #define DeviceBuildDir "build-x86"
#elif BuildArch == "x64"
  #define Tabor8200Name "Tabor 8200 Control"
  #define DeviceAppId "{{491860F6-AFD9-410B-9161-35DC20739F1A}"
  #define DeviceBuildDir "build"
#else
  #error BuildArch must be x86 or x64.
#endif
#define Tabor8200Exe "Tabor8200-Control.exe"

[Setup]
AppId={#DeviceAppId}
AppName={#Tabor8200Name}
AppVersion={#Tabor8200Version}
AppVerName={#Tabor8200Name} {#Tabor8200Version}
AppPublisher=Ralf Tralow
VersionInfoVersion={#Tabor8200Version}
VersionInfoDescription={#Tabor8200Name} Setup
DefaultDirName={pf}\{#Tabor8200Name}
DefaultGroupName={#Tabor8200Name}
AllowNoIcons=yes
PrivilegesRequired=admin
#if BuildArch == "x64"
ArchitecturesAllowed=x64
ArchitecturesInstallIn64BitMode=x64
#else
ArchitecturesAllowed=x86 x64
#endif
; Inno 5.3.5 can see modern Windows as 6.2 due to its legacy manifest.
SetupIconFile=assets\{#Tabor8200Icon}
UninstallDisplayIcon={app}\{#Tabor8200Exe}
OutputDir=installer
OutputBaseFilename=Tabor8200-Control-Setup-{#Tabor8200Version}-{#BuildArch}
Compression=lzma
SolidCompression=yes

[Languages]
Name: "german"; MessagesFile: "compiler:Languages\German.isl"
Name: "english"; MessagesFile: "compiler:Default.isl"
Name: "french"; MessagesFile: "compiler:Languages\French.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; Flags: unchecked

[Files]
#if BuildArch == "x86"
Source: "{#DeviceBuildDir}\qt.conf"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#DeviceBuildDir}\bearer\*.dll"; DestDir: "{app}\bearer"; Flags: ignoreversion skipifsourcedoesntexist
#endif
Source: "{#DeviceBuildDir}\{#Tabor8200Exe}"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#DeviceBuildDir}\*.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#DeviceBuildDir}\generic\*.dll"; DestDir: "{app}\generic"; Flags: ignoreversion skipifsourcedoesntexist
Source: "{#DeviceBuildDir}\iconengines\*.dll"; DestDir: "{app}\iconengines"; Flags: ignoreversion skipifsourcedoesntexist
Source: "{#DeviceBuildDir}\imageformats\*.dll"; DestDir: "{app}\imageformats"; Flags: ignoreversion skipifsourcedoesntexist
Source: "{#DeviceBuildDir}\networkinformation\*.dll"; DestDir: "{app}\networkinformation"; Flags: ignoreversion skipifsourcedoesntexist
Source: "{#DeviceBuildDir}\platforms\*.dll"; DestDir: "{app}\platforms"; Flags: ignoreversion
Source: "{#DeviceBuildDir}\styles\*.dll"; DestDir: "{app}\styles"; Flags: ignoreversion skipifsourcedoesntexist
Source: "{#DeviceBuildDir}\tls\*.dll"; DestDir: "{app}\tls"; Flags: ignoreversion skipifsourcedoesntexist
Source: "LICENSE"; DestDir: "{app}"; Flags: ignoreversion
Source: "THIRD-PARTY-NOTICES.txt"; DestDir: "{app}"; Flags: ignoreversion
Source: "licenses\*.txt"; DestDir: "{app}\licenses"; Flags: ignoreversion

[Icons]
Name: "{group}\{#Tabor8200Name}"; Filename: "{app}\{#Tabor8200Exe}"; WorkingDir: "{app}"
Name: "{commondesktop}\{#Tabor8200Name}"; Filename: "{app}\{#Tabor8200Exe}"; WorkingDir: "{app}"; Tasks: desktopicon
