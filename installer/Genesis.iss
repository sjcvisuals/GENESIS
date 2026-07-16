; GENESIS Windows installer - built via scripts/package-installer.ps1
; Requires Inno Setup 6: https://jrsoftware.org/isinfo.php

#ifndef StagingDir
  #define StagingDir "..\build\package\staging"
#endif

#ifndef MyAppVersion
  #define MyAppVersion "0.1.1"
#endif

[Setup]
AppId={{A7B3C4D5-E6F7-4890-ABCD-EF1234567890}
AppName=GENESIS
AppVersion={#MyAppVersion}
AppPublisher=GENESIS
DefaultDirName={autopf}\GENESIS
DefaultGroupName=GENESIS
DisableProgramGroupPage=yes
OutputDir=..\build\package
OutputBaseFilename=GenesisSetup-{#MyAppVersion}
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
PrivilegesRequired=admin
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
UninstallDisplayIcon={code:GetUninstallIcon}
SetupLogging=yes
InfoBeforeFile=INFO-BEFORE.txt

[Types]
Name: "full"; Description: "Full installation (Editor, Player tools, and PIXERA extension)"
Name: "playback"; Description: "Playback only (Player tools + PIXERA extension)"
Name: "custom"; Description: "Custom installation"; Flags: iscustom

[Components]
Name: "editor"; Description: "Genesis Editor - single-monitor scene authoring"; Types: full custom
Name: "player"; Description: "Genesis Player tools - preview, multi-view topology, validation"; Types: full playback custom
Name: "pixera"; Description: "PIXERA RenderBridge extension (ExtGenesis)"; Types: full playback custom

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
Name: "{group}\Genesis Editor"; Filename: "{app}\Editor\genesis_gltf_viewer.exe"; WorkingDir: "{app}\Editor"; Components: editor
Name: "{group}\Genesis Player"; Filename: "{app}\Player\genesis_player.exe"; WorkingDir: "{app}\Player"; Components: player
Name: "{group}\Genesis Multi-View Player"; Filename: "{app}\Player\genesis_multiview_player.exe"; WorkingDir: "{app}\Player"; Parameters: "assets\topology\examples\one_machine_one_gpu_one_4k50.topology.json demo --frames 30"; Components: player
Name: "{group}\Uninstall GENESIS"; Filename: "{uninstallexe}"

[Run]
Filename: "{tmp}\vc_redist.x64.exe"; Parameters: "/install /quiet /norestart"; StatusMsg: "Installing Microsoft Visual C++ runtime..."; Flags: waituntilterminated; Check: VCRedistNeedsInstall

[Code]
var
  PixeraPage: TInputDirWizardPage;
  PixeraExtDir: String;

function GetUninstallIcon(Param: String): String;
begin
  if FileExists(ExpandConstant('{app}\Editor\genesis_gltf_viewer.exe')) then
    Result := ExpandConstant('{app}\Editor\genesis_gltf_viewer.exe')
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
    'PIXERA Installation Folder',
    'Where should the RenderBridge extension be installed?',
    'Select your PIXERA root folder. GENESIS will install to data\rxExt\ExtGenesis\ inside it.' + #13#10 +
    'Skip this page by deselecting the PIXERA component on the previous screen.',
    False, 'New Folder');

  DefaultPixeraRoot := '';
  if DirExists(ExpandConstant('{pf}\AV Stumpfl\PIXERA')) then
    DefaultPixeraRoot := ExpandConstant('{pf}\AV Stumpfl\PIXERA')
  else if DirExists('C:\PIXERA') then
    DefaultPixeraRoot := 'C:\PIXERA';

  PixeraPage.Add('');
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
      MsgBox('Please select the PIXERA installation folder.', mbError, MB_OK);
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
