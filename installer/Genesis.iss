; GENESIS Windows installer — big-company Setup.exe wizard
; Built via scripts/package-installer.ps1 (requires Inno Setup 6)

#ifndef StagingDir
  #define StagingDir "..\build\package\staging"
#endif

#ifndef MyAppVersion
  #define MyAppVersion "0.1.1"
#endif

#define MyAppName "GENESIS"
#define MyAppPublisher "SJC Visuals"
#define MyAppURL "https://github.com/sjcvisuals/GENESIS"
#define MyAppExeName "genesis_gltf_viewer.exe"

[Setup]
AppId={{A7B3C4D5-E6F7-4890-ABCD-EF1234567890}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppVerName={#MyAppName} {#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppPublisherURL={#MyAppURL}
AppSupportURL={#MyAppURL}/issues
AppUpdatesURL={#MyAppURL}/releases
DefaultDirName={autopf}\GENESIS
DefaultGroupName=GENESIS
DisableProgramGroupPage=yes
DisableWelcomePage=no
AllowNoIcons=yes
OutputDir=..\build\package
OutputBaseFilename=GenesisSetup-{#MyAppVersion}
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
WizardSizePercent=120
PrivilegesRequired=admin
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
UninstallDisplayIcon={code:GetUninstallIcon}
UninstallDisplayName={#MyAppName}
SetupLogging=yes
InfoBeforeFile=WELCOME.txt
VersionInfoVersion={#MyAppVersion}.0
VersionInfoCompany={#MyAppPublisher}
VersionInfoDescription={#MyAppName} Setup
VersionInfoProductName={#MyAppName}
VersionInfoProductVersion={#MyAppVersion}
MinVersion=10.0
CloseApplications=yes
RestartApplications=no
UsedUserAreasWarning=no
ShowLanguageDialog=no
; Plain-language wizard copy (overrides Inno defaults)
WizardImageStretch=yes

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Messages]
WelcomeLabel1=Welcome to GENESIS Setup
WelcomeLabel2=This will install GENESIS on your computer.%n%nWe recommend closing other apps before continuing. Click Next to get started.
FinishedHeadingLabel=GENESIS is ready
FinishedLabelNoIcons=Setup finished installing GENESIS on your computer.
FinishedLabel=Setup finished installing GENESIS on your computer. You can open it from the Start menu or the desktop shortcut.
ClickFinish=Click Finish to exit Setup.
SelectComponentsLabel2=Choose what to install. Most people can leave Full installation selected.
SelectDirLabel3=Setup will install GENESIS into the following folder. Click Browse if you want a different location.
SelectDirBrowseLabel=Click Next to continue, or Browse to pick another folder.
ReadyLabel1=Setup is ready to install GENESIS.
ReadyLabel2a=Click Install to begin.
PreparingDesc=Setup is preparing to install GENESIS…
StatusExtractFiles=Copying files…
StatusCreateIcons=Creating shortcuts…
StatusCreateDirs=Creating folders…
StatusRegisterFiles=Finishing up…

[Types]
Name: "full"; Description: "Full installation (recommended)"
Name: "playback"; Description: "Playback only"
Name: "custom"; Description: "Custom"; Flags: iscustom

[Components]
Name: "editor"; Description: "GENESIS Editor — create and edit scenes"; Types: full custom
Name: "player"; Description: "Playback tools — preview on this computer"; Types: full playback custom
Name: "pixera"; Description: "PIXERA extension — only if you use PIXERA"; Types: full playback custom

[Tasks]
Name: "desktopicon"; Description: "Create a desktop shortcut"; GroupDescription: "Additional shortcuts:"; Flags: checkedonce
Name: "quicklaunchicon"; Description: "Create a Quick Launch shortcut"; GroupDescription: "Additional shortcuts:"; Flags: unchecked; OnlyBelowVersion: 6.1

[Files]
; Editor
Source: "{#StagingDir}\Editor\*"; DestDir: "{app}\Editor"; Components: editor; Flags: ignoreversion recursesubdirs createallsubdirs
; Player (playback + topology tools)
Source: "{#StagingDir}\Player\*"; DestDir: "{app}\Player"; Components: player; Flags: ignoreversion recursesubdirs createallsubdirs
; PIXERA extension (destination chosen on wizard page)
Source: "{#StagingDir}\Pixera\*"; DestDir: "{code:GetPixeraExtDir}"; Components: pixera; Flags: ignoreversion recursesubdirs createallsubdirs
; VC++ redistributable (extracted at install time, removed after)
Source: "{#StagingDir}\redist\vc_redist.x64.exe"; DestDir: "{tmp}"; Flags: deleteafterinstall; Check: VCRedistNeedsInstall

[Icons]
Name: "{group}\GENESIS"; Filename: "{app}\Editor\{#MyAppExeName}"; WorkingDir: "{app}\Editor"; Components: editor
Name: "{group}\GENESIS Player"; Filename: "{app}\Player\genesis_player.exe"; WorkingDir: "{app}\Player"; Components: player
Name: "{group}\Uninstall GENESIS"; Filename: "{uninstallexe}"
Name: "{autodesktop}\GENESIS"; Filename: "{app}\Editor\{#MyAppExeName}"; WorkingDir: "{app}\Editor"; Tasks: desktopicon; Components: editor

[Run]
Filename: "{tmp}\vc_redist.x64.exe"; Parameters: "/install /quiet /norestart"; StatusMsg: "Installing required Windows components…"; Flags: waituntilterminated; Check: VCRedistNeedsInstall
Filename: "{app}\Editor\{#MyAppExeName}"; Description: "Open GENESIS now"; Flags: nowait postinstall skipifsilent; Components: editor; WorkingDir: "{app}\Editor"

[Code]
var
  PixeraPage: TInputDirWizardPage;
  PixeraExtDir: String;

function GetUninstallIcon(Param: String): String;
begin
  if FileExists(ExpandConstant('{app}\Editor\{#MyAppExeName}')) then
    Result := ExpandConstant('{app}\Editor\{#MyAppExeName}')
  else if FileExists(ExpandConstant('{app}\Player\genesis_player.exe')) then
    Result := ExpandConstant('{app}\Player\genesis_player.exe')
  else
    Result := '';
end;

function VCRedistNeedsInstall: Boolean;
var
  Version: String;
begin
  Result := not RegQueryStringValue(HKLM, 'SOFTWARE\Microsoft\VisualStudio\14.0\VC\Runtimes\x64', 'Version', Version);
end;

procedure InitializeWizard;
var
  DefaultPixeraRoot: String;
begin
  PixeraPage := CreateInputDirPage(wpSelectComponents,
    'PIXERA folder (optional)',
    'Only needed if you selected the PIXERA extension.',
    'Choose the folder where PIXERA is installed. GENESIS will place its extension in the right subfolder automatically.' + #13#10 + #13#10 +
    'If you are not using PIXERA, go Back and turn off the PIXERA extension component.',
    False, 'New Folder');

  DefaultPixeraRoot := '';
  if DirExists(ExpandConstant('{pf}\AV Stumpfl\PIXERA')) then
    DefaultPixeraRoot := ExpandConstant('{pf}\AV Stumpfl\PIXERA')
  else if DirExists('C:\PIXERA') then
    DefaultPixeraRoot := 'C:\PIXERA';

  PixeraPage.Add('PIXERA installation folder:');
  if DefaultPixeraRoot <> '' then
    PixeraPage.Values[0] := DefaultPixeraRoot;
end;

function ShouldSkipPage(PageID: Integer): Boolean;
begin
  Result := False;
  if PageID = PixeraPage.ID then
    Result := not WizardIsComponentSelected('pixera');
end;

function NextButtonClick(CurPageID: Integer): Boolean;
begin
  Result := True;
  if CurPageID = PixeraPage.ID then
  begin
    if PixeraPage.Values[0] = '' then
    begin
      MsgBox('Please choose your PIXERA folder, or go Back and turn off the PIXERA extension.', mbError, MB_OK);
      Result := False;
    end
    else
      PixeraExtDir := AddBackslash(PixeraPage.Values[0]) + 'data\rxExt\ExtGenesis';
  end;
end;

function GetPixeraExtDir(Param: String): String;
begin
  if PixeraExtDir <> '' then
    Result := PixeraExtDir
  else
    Result := ExpandConstant('{app}\PixeraExtension');
end;
