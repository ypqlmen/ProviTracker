#define MyAppName "Provi Tracker"
#define MyAppExeName "ProvisionTrackerV2.exe"
#define MyAppPublisher "Victor Tang"
#define MyAppVersion "1.3.24"
#define MySetupBaseName "ProviBeregnerSetup-1.3.24"
#ifndef BuildDir
  #define BuildDir "..\build\installer_staging"
#endif

[Setup]
AppId={{6B532F89-64F6-44F3-A68B-24E00D8F1E8B}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
DefaultDirName={localappdata}\Programs\Provi Tracker
DefaultGroupName={#MyAppName}
DisableProgramGroupPage=yes
OutputDir=..\dist
OutputBaseFilename={#MySetupBaseName}
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
UninstallDisplayName={#MyAppName}
UninstallDisplayIcon={app}\{#MyAppExeName}
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
Source: "{#BuildDir}\ProvisionTrackerV2.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#BuildDir}\*.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#BuildDir}\opengl32sw.dll"; DestDir: "{app}"; Flags: ignoreversion skipifsourcedoesntexist
Source: "{#BuildDir}\D3Dcompiler_47.dll"; DestDir: "{app}"; Flags: ignoreversion skipifsourcedoesntexist
Source: "{#BuildDir}\platforms\*"; DestDir: "{app}\platforms"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#BuildDir}\styles\*"; DestDir: "{app}\styles"; Flags: ignoreversion recursesubdirs createallsubdirs skipifsourcedoesntexist
Source: "{#BuildDir}\iconengines\*"; DestDir: "{app}\iconengines"; Flags: ignoreversion recursesubdirs createallsubdirs skipifsourcedoesntexist
Source: "{#BuildDir}\imageformats\*"; DestDir: "{app}\imageformats"; Flags: ignoreversion recursesubdirs createallsubdirs skipifsourcedoesntexist
Source: "{#BuildDir}\networkinformation\*"; DestDir: "{app}\networkinformation"; Flags: ignoreversion recursesubdirs createallsubdirs skipifsourcedoesntexist
Source: "{#BuildDir}\generic\*"; DestDir: "{app}\generic"; Flags: ignoreversion recursesubdirs createallsubdirs skipifsourcedoesntexist
Source: "{#BuildDir}\tls\*"; DestDir: "{app}\tls"; Flags: ignoreversion recursesubdirs createallsubdirs skipifsourcedoesntexist
Source: "{#BuildDir}\translations\qt_da.qm"; DestDir: "{app}\translations"; Flags: ignoreversion skipifsourcedoesntexist
Source: "{#BuildDir}\translations\qt_en.qm"; DestDir: "{app}\translations"; Flags: ignoreversion skipifsourcedoesntexist
Source: "{#BuildDir}\intramanager_worker\intramanager_sync.exe"; DestDir: "{app}\intramanager_worker"; Flags: ignoreversion
Source: "{#BuildDir}\intramanager_worker\_internal\*"; DestDir: "{app}\intramanager_worker\_internal"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#BuildDir}\intramanager_worker\b\*"; DestDir: "{app}\intramanager_worker\b"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon

[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "{cm:LaunchProgram,{#StringChange(MyAppName, '&', '&&')}}"; Flags: nowait postinstall skipifsilent

[Code]
const
  Legacy11UninstallKey = 'Software\Microsoft\Windows\CurrentVersion\Uninstall\{CBA2670F-F574-46E0-8913-FBEF7822C1B7}_is1';

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

procedure UninstallLegacy11IfSeparate(LegacyDir: string);
var
  UninstallString: string;
  Uninstaller: string;
  ResultCode: Integer;
begin
  if (LegacyDir = '') or SameDir(LegacyDir, ExpandConstant('{app}')) then
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
