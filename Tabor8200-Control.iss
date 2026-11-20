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
#define Tabor8200Name "Tabor 8200 Control"
#define Tabor8200Exe "Tabor8200-Control.exe"

[Setup]
AppId={{491860F6-AFD9-410B-9161-35DC20739F1A}
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
ArchitecturesAllowed=x64
ArchitecturesInstallIn64BitMode=x64
; Inno 5.3.5 can see modern Windows as 6.2 due to its legacy manifest.
SetupIconFile=assets\{#Tabor8200Icon}
UninstallDisplayIcon={app}\{#Tabor8200Exe}
OutputDir=installer
OutputBaseFilename=Tabor8200-Control-Setup-{#Tabor8200Version}-x64
Compression=lzma
SolidCompression=yes

[Languages]
Name: "german"; MessagesFile: "compiler:Languages\German.isl"
Name: "english"; MessagesFile: "compiler:Default.isl"
Name: "french"; MessagesFile: "compiler:Languages\French.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; Flags: unchecked

[Files]
Source: "build\{#Tabor8200Exe}"; DestDir: "{app}"; Flags: ignoreversion
Source: "build\*.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "build\generic\*.dll"; DestDir: "{app}\generic"; Flags: ignoreversion
Source: "build\iconengines\*.dll"; DestDir: "{app}\iconengines"; Flags: ignoreversion
Source: "build\imageformats\*.dll"; DestDir: "{app}\imageformats"; Flags: ignoreversion
Source: "build\networkinformation\*.dll"; DestDir: "{app}\networkinformation"; Flags: ignoreversion
Source: "build\platforms\*.dll"; DestDir: "{app}\platforms"; Flags: ignoreversion
Source: "build\styles\*.dll"; DestDir: "{app}\styles"; Flags: ignoreversion
Source: "build\tls\*.dll"; DestDir: "{app}\tls"; Flags: ignoreversion
Source: "LICENSE"; DestDir: "{app}"; Flags: ignoreversion
Source: "THIRD-PARTY-NOTICES.txt"; DestDir: "{app}"; Flags: ignoreversion
Source: "licenses\*.txt"; DestDir: "{app}\licenses"; Flags: ignoreversion

[Icons]
Name: "{group}\{#Tabor8200Name}"; Filename: "{app}\{#Tabor8200Exe}"; WorkingDir: "{app}"
Name: "{commondesktop}\{#Tabor8200Name}"; Filename: "{app}\{#Tabor8200Exe}"; WorkingDir: "{app}"; Tasks: desktopicon
