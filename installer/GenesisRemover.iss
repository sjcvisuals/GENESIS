; Genesis Remover — free PIXERA background-cutout installer
; Built via scripts/package-remover-installer.ps1
; Requires Inno Setup 6: https://jrsoftware.org/isinfo.php

#ifndef StagingDir
  #define StagingDir "..\build\package\remover-staging"
#endif

#ifndef MyAppVersion
  #define MyAppVersion "0.1.0"
#endif

[Setup]
AppId={{B8C4D5E6-F7A8-4901-BCDE-F12345678901}
AppName=Genesis Remover
AppVersion={#MyAppVersion}
AppPublisher=GENESIS
AppPublisherURL=https://github.com/sjcvisuals/GENESIS
AppSupportURL=https://github.com/sjcvisuals/GENESIS
DefaultDirName={autopf}\Genesis Remover
DefaultGroupName=Genesis Remover
DisableProgramGroupPage=yes
OutputDir=..\build\package
OutputBaseFilename=GenesisRemoverSetup-{#MyAppVersion}
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
PrivilegesRequired=admin
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
SetupLogging=yes
InfoBeforeFile=INFO-REMOVER-BEFORE.txt
LicenseFile=
UninstallDisplayName=Genesis Remover (PIXERA)

[Types]
Name: "pixera"; Description: "Install into PIXERA (recommended)"
Name: "portable"; Description: "Portable folder (browse from PIXERA generic extension)"
Name: "custom"; Description: "Custom"; Flags: iscustom

[Components]
Name: "extension"; Description: "ExtGenesisRemover RenderBridge module"; Types: pixera portable custom; Flags: fixed
Name: "vcredist"; Description: "Visual C++ 2015-2022 Redistributable (x64)"; Types: pixera portable custom

[Files]
Source: "{#StagingDir}\ExtGenesisRemover\*"; DestDir: "{code:GetRemoverInstallDir}"; Components: extension; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#StagingDir}\redist\vc_redist.x64.exe"; DestDir: "{tmp}"; Flags: deleteafterinstall; Components: vcredist; Check: VCRedistNeedsInstall

[Icons]
Name: "{group}\Open Genesis Remover folder"; Filename: "{code:GetRemoverInstallDir}"; Components: extension
Name: "{group}\How to use in PIXERA"; Filename: "{code:GetRemoverInstallDir}\README.txt"; Components: extension
Name: "{group}\Uninstall Genesis Remover"; Filename: "{uninstallexe}"

[Run]
Filename: "{tmp}\vc_redist.x64.exe"; Parameters: "/install /quiet /norestart"; StatusMsg: "Installing Microsoft Visual C++ runtime..."; Flags: waituntilterminated; Components: vcredist; Check: VCRedistNeedsInstall
Filename: "{code:GetRemoverInstallDir}"; Description: "Open installed folder (point PIXERA generic extension here)"; Flags: postinstall shellexec skipifsilent unchecked

[Code]
var
  PixeraPage: TInputDirWizardPage;
  RemoverInstallDir: String;
  InstallMode: String; // 'pixera' or 'portable'

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
  InstallMode := 'pixera';
  RemoverInstallDir := '';

  PixeraPage := CreateInputDirPage(wpSelectComponents,
    'PIXERA Installation Folder',
    'Where should Genesis Remover be installed for PIXERA?',
    'Select your PIXERA root folder. The module will go to data\rxExt\ExtGenesisRemover\.' + #13#10 +
    'Or choose Portable on the previous screen to install under Program Files and browse from PIXERA.',
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
  begin
    { Skip PIXERA path page when user chose portable install type }
    if WizardSetupType(False) = 'portable' then
      Result := True;
  end;
end;

function NextButtonClick(CurPageID: Integer): Boolean;
begin
  Result := True;
  if CurPageID = wpSelectComponents then
  begin
    if WizardSetupType(False) = 'portable' then
    begin
      InstallMode := 'portable';
      RemoverInstallDir := ExpandConstant('{app}');
    end
    else
      InstallMode := 'pixera';
  end;

  if CurPageID = PixeraPage.ID then
  begin
    if PixeraPage.Values[0] = '' then
    begin
      MsgBox('Please select the PIXERA installation folder, or go back and choose Portable.', mbError, MB_OK);
      Result := False;
    end
    else
    begin
      InstallMode := 'pixera';
      RemoverInstallDir := AddBackslash(PixeraPage.Values[0]) + 'data\rxExt\ExtGenesisRemover';
    end;
  end;
end;

function GetRemoverInstallDir(Param: String): String;
begin
  if RemoverInstallDir <> '' then
    Result := RemoverInstallDir
  else if InstallMode = 'portable' then
    Result := ExpandConstant('{app}')
  else
    Result := ExpandConstant('{app}');
end;

function UpdateReadyMemo(Space, NewLine, MemoUserInfoInfo, MemoDirInfo, MemoTypeInfo,
  MemoComponentsInfo, MemoGroupInfo, MemoTasksInfo: String): String;
var
  S: String;
begin
  S := 'Genesis Remover will install to:' + NewLine + Space + GetRemoverInstallDir('') + NewLine + NewLine;
  S := S + 'After install:' + NewLine;
  S := S + Space + '1. In PIXERA Compositing, add a generic RenderBridge extension.' + NewLine;
  S := S + Space + '2. Browse to ExtGenesisRemover.dll in the folder above.' + NewLine;
  S := S + Space + '3. Drag your SDI / NDI / media resource onto Video In.' + NewLine;
  S := S + Space + '4. Output is a realtime transparent person cutout.' + NewLine + NewLine;
  S := S + MemoTypeInfo + NewLine + MemoComponentsInfo;
  Result := S;
end;
