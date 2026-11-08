#define PluginName "Vocal Auto Assistant"
#define PluginVersion "0.2.0"
[Setup]
AppId={{74ED46B8-3FE0-4DA1-A5DD-8BD885814E44}
AppName={#PluginName}
AppVersion={#PluginVersion}
AppPublisher=Vocal Tools
DefaultDirName={commonpf}\Common Files\VST3
DisableDirPage=yes
DisableProgramGroupPage=yes
UninstallDisplayName={#PluginName} VST3
OutputDir=dist
OutputBaseFilename=Vocal_Auto_Assistant_Setup_Windows
Compression=lzma2
SolidCompression=yes
ArchitecturesAllowed=x64
ArchitecturesInstallIn64BitMode=x64
PrivilegesRequired=admin
WizardStyle=modern
[Files]
Source: "build\VocalAutoAssistant_artefacts\Release\VST3\Vocal Auto Assistant.vst3\*"; DestDir: "{app}\Vocal Auto Assistant.vst3"; Flags: ignoreversion recursesubdirs createallsubdirs
[Messages]
WelcomeLabel1=Install Vocal Auto Assistant
WelcomeLabel2=This setup installs the VST3 plugin for FL Studio and compatible audio applications.
