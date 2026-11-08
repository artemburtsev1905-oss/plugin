#define Name "Priora Bus Bust05 by DJ Begemot"
#define Version "0.5.0"
[Setup]
AppId={{5E38DCC6-E5B5-4E54-A068-FE82C3933BD1}
AppName={#Name}
AppVersion={#Version}
AppPublisher=DJ Begemot
DefaultDirName={commoncf64}\VST3
DisableDirPage=yes
DisableProgramGroupPage=yes
UninstallDisplayName=Priora Bus Bust05 VST3
OutputDir=dist
OutputBaseFilename=Priora_Bus_Bust05_by_DJ_Begemot_Setup
Compression=lzma2
SolidCompression=yes
ArchitecturesAllowed=x64
ArchitecturesInstallIn64BitMode=x64
PrivilegesRequired=admin
WizardStyle=modern
[Files]
Source: "build\PrioraBusBust05_artefacts\Release\VST3\Priora Bus Bust05 by DJ Begemot.vst3\*"; DestDir: "{app}\Priora Bus Bust05 by DJ Begemot.vst3"; Flags: ignoreversion recursesubdirs createallsubdirs
