#define MyAppName "Provi Tracker"
#define MyAppExeName "ProvisionTrackerV2.exe"
#define MyAppPublisher "Victor Tang"
#define MyAppVersion "1.5.9"
#define MySetupBaseName "ProviBeregnerSetup-1.5.9"
#define UserInstallDir "{localappdata}\Programs\Provi Tracker"
#ifndef BuildDir
  #define BuildDir "..\build\installer_staging"
#endif

[Setup]
AppId={{6B532F89-64F6-44F3-A68B-24E00D8F1E8B}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
DefaultDirName={#UserInstallDir}
DefaultGroupName={#MyAppName}
DisableProgramGroupPage=yes
CreateAppDir=no
OutputDir=..\dist
OutputBaseFilename={#MySetupBaseName}
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
UninstallDisplayName={#MyAppName}
UninstallDisplayIcon={#UserInstallDir}\{#MyAppExeName}
UninstallFilesDir={#UserInstallDir}
CloseApplications=yes
RestartApplications=no
VersionInfoVersion={#MyAppVersion}
VersionInfoCompany={#MyAppPublisher}
VersionInfoDescription={#MyAppName} installer
VersionInfoProductName={#MyAppName}
VersionInfoProductVersion={#MyAppVersion}

[Languages]
Name: "danish"; MessagesFile: "compiler:Languages\Danish.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
Source: "{#BuildDir}\ProvisionTrackerV2.exe"; DestDir: "{#UserInstallDir}"; Flags: ignoreversion
Source: "{#BuildDir}\*.dll"; DestDir: "{#UserInstallDir}"; Flags: ignoreversion
Source: "{#BuildDir}\opengl32sw.dll"; DestDir: "{#UserInstallDir}"; Flags: ignoreversion skipifsourcedoesntexist
Source: "{#BuildDir}\D3Dcompiler_47.dll"; DestDir: "{#UserInstallDir}"; Flags: ignoreversion skipifsourcedoesntexist
Source: "{#BuildDir}\platforms\*"; DestDir: "{#UserInstallDir}\platforms"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#BuildDir}\styles\*"; DestDir: "{#UserInstallDir}\styles"; Flags: ignoreversion recursesubdirs createallsubdirs skipifsourcedoesntexist
Source: "{#BuildDir}\iconengines\*"; DestDir: "{#UserInstallDir}\iconengines"; Flags: ignoreversion recursesubdirs createallsubdirs skipifsourcedoesntexist
Source: "{#BuildDir}\imageformats\*"; DestDir: "{#UserInstallDir}\imageformats"; Flags: ignoreversion recursesubdirs createallsubdirs skipifsourcedoesntexist
Source: "{#BuildDir}\networkinformation\*"; DestDir: "{#UserInstallDir}\networkinformation"; Flags: ignoreversion recursesubdirs createallsubdirs skipifsourcedoesntexist
Source: "{#BuildDir}\generic\*"; DestDir: "{#UserInstallDir}\generic"; Flags: ignoreversion recursesubdirs createallsubdirs skipifsourcedoesntexist
Source: "{#BuildDir}\tls\*"; DestDir: "{#UserInstallDir}\tls"; Flags: ignoreversion recursesubdirs createallsubdirs skipifsourcedoesntexist
Source: "{#BuildDir}\translations\qt_da.qm"; DestDir: "{#UserInstallDir}\translations"; Flags: ignoreversion skipifsourcedoesntexist
Source: "{#BuildDir}\translations\qt_en.qm"; DestDir: "{#UserInstallDir}\translations"; Flags: ignoreversion skipifsourcedoesntexist
Source: "{#BuildDir}\intramanager_worker\intramanager_sync.exe"; DestDir: "{#UserInstallDir}\intramanager_worker"; Flags: ignoreversion
Source: "{#BuildDir}\intramanager_worker\_internal\*"; DestDir: "{#UserInstallDir}\intramanager_worker\_internal"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#BuildDir}\intramanager_worker\b\*"; DestDir: "{#UserInstallDir}\intramanager_worker\b"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\{#MyAppName}"; Filename: "{#UserInstallDir}\{#MyAppExeName}"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{#UserInstallDir}\{#MyAppExeName}"; Tasks: desktopicon
Name: "{autodesktop}\{#MyAppName}"; Filename: "{#UserInstallDir}\{#MyAppExeName}"; Check: ShouldRefreshDesktopShortcut
Name: "{autodesktop}\Provi Beregner"; Filename: "{#UserInstallDir}\{#MyAppExeName}"; Check: ShouldRefreshLegacyDesktopShortcut('Provi Beregner')
Name: "{autodesktop}\ProvisionTrackerV2"; Filename: "{#UserInstallDir}\{#MyAppExeName}"; Check: ShouldRefreshLegacyDesktopShortcut('ProvisionTrackerV2')
Name: "{userprograms}\Provi Beregner"; Filename: "{#UserInstallDir}\{#MyAppExeName}"; Check: ShouldRefreshLegacyStartMenuShortcut('Provi Beregner')
Name: "{userprograms}\ProvisionTrackerV2"; Filename: "{#UserInstallDir}\{#MyAppExeName}"; Check: ShouldRefreshLegacyStartMenuShortcut('ProvisionTrackerV2')

[Run]
Filename: "{#UserInstallDir}\{#MyAppExeName}"; Description: "{cm:LaunchProgram,{#StringChange(MyAppName, '&', '&&')}}"; Flags: nowait postinstall skipifsilent
Filename: "{win}\explorer.exe"; Parameters: """{#UserInstallDir}\{#MyAppExeName}"""; Flags: nowait; Check: ShouldLaunchRedirectedSilent

[Code]
const
  Legacy11UninstallKey = 'Software\Microsoft\Windows\CurrentVersion\Uninstall\{CBA2670F-F574-46E0-8913-FBEF7822C1B7}_is1';

function ShouldRefreshDesktopShortcut: Boolean;
begin
  Result := WizardSilent;
end;

function ShouldRefreshLegacyDesktopShortcut(Name: string): Boolean;
begin
  Result := WizardSilent and FileExists(ExpandConstant('{autodesktop}\' + Name + '.lnk'));
end;

function ShouldRefreshLegacyStartMenuShortcut(Name: string): Boolean;
begin
  Result := WizardSilent and FileExists(ExpandConstant('{userprograms}\' + Name + '.lnk'));
end;

procedure CopyFileIfMissing(Source, Target: string);
begin
  if FileExists(Source) and not FileExists(Target) then begin
    ForceDirectories(ExtractFileDir(Target));
    CopyFile(Source, Target, True);
  end;
end;

procedure CopyDirContents(SourceDir, TargetDir: string);
var
  FindRec: TFindRec;
  SourcePath: string;
  TargetPath: string;
begin
  if not DirExists(SourceDir) then
    exit;

  ForceDirectories(TargetDir);

  if FindFirst(AddBackslash(SourceDir) + '*', FindRec) then begin
    try
      repeat
        if (FindRec.Name <> '.') and (FindRec.Name <> '..') then begin
          SourcePath := AddBackslash(SourceDir) + FindRec.Name;
          TargetPath := AddBackslash(TargetDir) + FindRec.Name;
          if (FindRec.Attributes and FILE_ATTRIBUTE_DIRECTORY) <> 0 then begin
            if not DirExists(TargetPath) then
              CopyDirContents(SourcePath, TargetPath);
          end else begin
            CopyFileIfMissing(SourcePath, TargetPath);
          end;
        end;
      until not FindNext(FindRec);
    finally
      FindClose(FindRec);
    end;
  end;
end;

procedure CopyDirIfMissing(SourceDir, TargetDir: string);
begin
  if DirExists(SourceDir) and not DirExists(TargetDir) then
    CopyDirContents(SourceDir, TargetDir);
end;

procedure MigrateLegacyDataFrom(SourceDir: string);
var
  TargetDir: string;
begin
  if (SourceDir = '') or not DirExists(SourceDir) then
    exit;

  TargetDir := ExpandConstant('{localappdata}\ProvisionTrackerV2\ProviTracker');

  CopyFileIfMissing(AddBackslash(SourceDir) + 'salespeople.json', AddBackslash(TargetDir) + 'salespeople.json');
  CopyFileIfMissing(AddBackslash(SourceDir) + 'products.json', AddBackslash(TargetDir) + 'products.json');
  CopyFileIfMissing(AddBackslash(SourceDir) + 'orders.json', AddBackslash(TargetDir) + 'orders.json');
  CopyFileIfMissing(AddBackslash(SourceDir) + 'settings.json', AddBackslash(TargetDir) + 'settings.json');
  CopyDirIfMissing(AddBackslash(SourceDir) + 'snapshots', AddBackslash(TargetDir) + 'snapshots');
  CopyDirIfMissing(AddBackslash(SourceDir) + 'reports', AddBackslash(TargetDir) + 'reports');
end;

function SameDir(Left, Right: string): Boolean;
begin
  Result := CompareText(RemoveBackslashUnlessRoot(Left), RemoveBackslashUnlessRoot(Right)) = 0;
end;

function UserInstallDirValue: string;
begin
  Result := ExpandConstant('{#UserInstallDir}');
end;

function CommandLineDirValue: string;
var
  I: Integer;
  Value: string;
begin
  Result := '';
  for I := 1 to ParamCount do begin
    Value := ParamStr(I);
    if CompareText(Copy(Value, 1, 5), '/DIR=') = 0 then begin
      Result := RemoveQuotes(Copy(Value, 6, Length(Value)));
      exit;
    end;
  end;
end;

function IsRedirectedAutoUpdate: Boolean;
var
  RequestedDir: string;
begin
  RequestedDir := CommandLineDirValue;
  Result := WizardSilent and (RequestedDir <> '') and not SameDir(RequestedDir, UserInstallDirValue);
end;

function ShouldLaunchRedirectedSilent: Boolean;
begin
  Result := IsRedirectedAutoUpdate;
end;

function GetCustomSetupExitCode: Integer;
begin
  if IsRedirectedAutoUpdate then
    Result := 42
  else
    Result := 0;
end;

procedure UninstallLegacy11IfSeparate(LegacyDir: string);
var
  UninstallString: string;
  Uninstaller: string;
  ResultCode: Integer;
begin
  if (LegacyDir = '') or SameDir(LegacyDir, UserInstallDirValue) then
    exit;

  if RegQueryStringValue(HKCU, Legacy11UninstallKey, 'UninstallString', UninstallString) then begin
    Uninstaller := RemoveQuotes(UninstallString);
    if FileExists(Uninstaller) then
      Exec(Uninstaller, '/VERYSILENT /SUPPRESSMSGBOXES /NORESTART', '', SW_HIDE, ewWaitUntilTerminated, ResultCode);
  end;
end;

procedure CurStepChanged(CurStep: TSetupStep);
var
  LegacyDir: string;
begin
  if CurStep = ssPostInstall then begin
    if RegQueryStringValue(HKCU, Legacy11UninstallKey, 'InstallLocation', LegacyDir) then begin
      MigrateLegacyDataFrom(LegacyDir);
      MigrateLegacyDataFrom(AddBackslash(LegacyDir) + 'data');
      UninstallLegacy11IfSeparate(LegacyDir);
    end;
  end;
end;
