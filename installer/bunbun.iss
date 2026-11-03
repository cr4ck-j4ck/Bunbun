; Bunbun's installer (Inno Setup 6). scripts/package.sh builds it from the
; staged release folder:
;
;   ISCC.exe /DAppVersion=0.2.0 /DAppVersionNumeric=0.2.0 /DSourceDir=<stage> /O<dist> installer\bunbun.iss
;
; It installs for the current user only (no admin prompt) into
; %LOCALAPPDATA%\Programs\Bunbun, adds Start menu shortcuts and an uninstaller,
; installs the WebView2 Runtime if Windows doesn't have it (the settings window
; needs it), and can download a local AI model for Ask Bun. Settings, skin
; packs and models live in the user's AppData and survive an uninstall.

#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif
#ifndef AppVersionNumeric
  #define AppVersionNumeric "0.0.0"
#endif
#ifndef SourceDir
  #error Pass /DSourceDir=<the staged release folder>
#endif

[Setup]
; Never change the AppId: it's how Windows knows a new version replaces this one.
AppId={{396E6B5D-FBA9-4F82-98F7-75FE2B0AC69D}
AppName=Bunbun
AppVersion={#AppVersion}
AppVerName=Bunbun {#AppVersion}
AppPublisher=Arya Sharma
AppPublisherURL=https://github.com/iaryasharma/Bunbun
AppSupportURL=https://github.com/iaryasharma/Bunbun/issues
AppUpdatesURL=https://github.com/iaryasharma/Bunbun/releases
VersionInfoVersion={#AppVersionNumeric}
DefaultDirName={localappdata}\Programs\Bunbun
DisableProgramGroupPage=yes
DisableDirPage=auto
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0.17763
OutputBaseFilename=Bunbun-{#AppVersion}-setup
SetupIconFile=..\res\bunbun.ico
UninstallDisplayIcon={app}\bunbun.exe
UninstallDisplayName=Bunbun
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
; Bunbun is quit through its own --quit (see PrepareToInstall), which also
; removes its tray icon cleanly.
CloseApplications=no

[Tasks]
Name: autostart; Description: "Start Bunbun when I sign in to Windows"
Name: localai; Description: "Set up Local AI for Ask Bun: download a small language model (about 1.1 GB) that runs offline on this PC"; Flags: unchecked

[Files]
Source: "{#SourceDir}\*"; DestDir: "{app}"; Excludes: "START HERE.txt"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{userprograms}\Bunbun"; Filename: "{app}\bunbun.exe"; Comment: "Dynamic Island for Windows"
Name: "{userprograms}\Bunbun Settings"; Filename: "{app}\bunbun-settings.exe"

[Registry]
; The same value the settings app's "Start with Windows" switch writes.
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; ValueType: string; ValueName: "Bunbun"; \
    ValueData: """{app}\bunbun.exe"""; Tasks: autostart

[Run]
Filename: "{app}\bunbun.exe"; Description: "Start Bunbun"; Flags: postinstall nowait skipifsilent

[UninstallRun]
; Quit, then take this copy's hooks out of the coding agents connected to it:
; an agent whose hook points at a deleted file can't run its tools. --if-ours
; leaves hooks that call another copy (a portable or development build) alone.
Filename: "{app}\bunbun.exe"; Parameters: "--quit"; Flags: runhidden waituntilterminated; RunOnceId: "Quit"
Filename: "{app}\bunbun.exe"; Parameters: "--disconnect claude --quiet --if-ours"; Flags: runhidden waituntilterminated; RunOnceId: "DisconnectClaude"
Filename: "{app}\bunbun.exe"; Parameters: "--disconnect antigravity --quiet --if-ours"; Flags: runhidden waituntilterminated; RunOnceId: "DisconnectAntigravity"
Filename: "{app}\bunbun.exe"; Parameters: "--disconnect cursor --quiet --if-ours"; Flags: runhidden waituntilterminated; RunOnceId: "DisconnectCursor"
Filename: "{app}\bunbun.exe"; Parameters: "--disconnect copilot --quiet --if-ours"; Flags: runhidden waituntilterminated; RunOnceId: "DisconnectCopilot"

[Code]
const
  RunKey = 'Software\Microsoft\Windows\CurrentVersion\Run';
  WebView2Id = '{F3017226-FE2A-4295-8BDF-00C3A9A7E4C5}';
  WebView2Url = 'https://go.microsoft.com/fwlink/p/?LinkId=2124703';
  WebView2File = 'MicrosoftEdgeWebview2Setup.exe';
  // The model docs/local-ai.md recommends; the hash is checked after download.
  ModelUrl = 'https://huggingface.co/Qwen/Qwen2.5-1.5B-Instruct-GGUF/resolve/main/qwen2.5-1.5b-instruct-q4_k_m.gguf';
  ModelFile = 'qwen2.5-1.5b-instruct-q4_k_m.gguf';
  ModelSha256 = '6a1a2eb6d15622bf3c96857206351ba97e1af16c30d7a74ee38970e434e9407e';

var
  DownloadPage: TDownloadWizardPage;
  GotWebView2, GotModel: Boolean;

function ModelPath: String;
begin
  Result := ExpandConstant('{localappdata}\Bunbun\models\' + ModelFile);
end;

function HasVersion(Root: Integer; Key: String): Boolean;
var
  V: String;
begin
  Result := RegQueryStringValue(Root, Key, 'pv', V) and (V <> '') and (V <> '0.0.0.0');
end;

// Installed machine-wide (Windows 11, Edge on Windows 10) or for this user.
function WebView2Installed: Boolean;
begin
  Result := HasVersion(HKLM, 'SOFTWARE\WOW6432Node\Microsoft\EdgeUpdate\Clients\' + WebView2Id) or
            HasVersion(HKCU, 'Software\Microsoft\EdgeUpdate\Clients\' + WebView2Id);
end;

function OnDownloadProgress(const Url, FileName: String; const Progress, ProgressMax: Int64): Boolean;
begin
  Result := True;
end;

procedure InitializeWizard;
begin
  DownloadPage := CreateDownloadPage(SetupMessage(msgWizardPreparing), SetupMessage(msgPreparingDesc), @OnDownloadProgress);
end;

// Downloads one file into {tmp}; False (after telling the user) if it failed or was cancelled.
function Fetch(Url, FileName, Sha256, What: String): Boolean;
begin
  DownloadPage.Clear;
  DownloadPage.Add(Url, FileName, Sha256);
  DownloadPage.Show;
  try
    try
      DownloadPage.Download;
      Result := True;
    except
      Result := False;
      if not DownloadPage.AbortedByUser then
        SuppressibleMsgBox(What + ' couldn''t be downloaded: ' + AddPeriod(GetExceptionMessage) +
          ' Setup will continue without it.', mbInformation, MB_OK, IDOK);
    end;
  finally
    DownloadPage.Hide;
  end;
end;

// Downloads happen when the user clicks Install, before any file is copied.
function NextButtonClick(CurPageID: Integer): Boolean;
begin
  Result := True;
  if CurPageID <> wpReady then exit;
  if not WebView2Installed then
    GotWebView2 := Fetch(WebView2Url, WebView2File, '', 'The WebView2 Runtime (needed by the settings window)');
  if WizardIsTaskSelected('localai') and not FileExists(ModelPath) then
    GotModel := Fetch(ModelUrl, ModelFile, ModelSha256, 'The Local AI model');
end;

// Renames {app}\<Name> to <Name>.old-<n> when it's there (works while it runs).
procedure MoveAside(Name: String);
var
  N: Integer;
begin
  if not FileExists(ExpandConstant('{app}\' + Name)) then exit;
  N := 0;
  while FileExists(ExpandConstant('{app}\' + Name + '.old-' + IntToStr(N))) do N := N + 1;
  RenameFile(ExpandConstant('{app}\' + Name), ExpandConstant('{app}\' + Name + '.old-' + IntToStr(N)));
end;

// An older Bunbun must let go of its files before they're replaced.
function PrepareToInstall(var NeedsRestart: Boolean): String;
var
  Code: Integer;
begin
  Result := '';
  if FileExists(ExpandConstant('{app}\bunbun.exe')) then
    Exec(ExpandConstant('{app}\bunbun.exe'), '--quit', '', SW_HIDE, ewWaitUntilTerminated, Code);
  // Coding agents keep bunbun-hook.exe running (a reply hook can wait half an hour),
  // and Windows won't overwrite a running exe but will rename it. The new copy goes
  // in beside it; Bunbun deletes the *.old files the next time it starts.
  MoveAside('bunbun-hook.exe');
  MoveAside('bunbun-brain.exe');
end;

procedure CurStepChanged(CurStep: TSetupStep);
var
  Code: Integer;
begin
  if CurStep <> ssPostInstall then exit;
  // A Bunbun running from somewhere else (an unzipped copy) gives way too.
  Exec(ExpandConstant('{app}\bunbun.exe'), '--quit', '', SW_HIDE, ewWaitUntilTerminated, Code);
  if GotWebView2 then
    Exec(ExpandConstant('{tmp}\' + WebView2File), '/silent /install', '', SW_HIDE, ewWaitUntilTerminated, Code);
  if GotModel then begin
    ForceDirectories(ExtractFileDir(ModelPath));
    // {tmp} is on the same drive as AppData, so this is a move, not a 1 GB copy.
    if not RenameFile(ExpandConstant('{tmp}\' + ModelFile), ModelPath) then
      FileCopy(ExpandConstant('{tmp}\' + ModelFile), ModelPath, False);
  end;
  if WizardIsTaskSelected('localai') and FileExists(ModelPath) then
    Exec(ExpandConstant('{app}\bunbun.exe'), '--enable-local-ai', '', SW_HIDE, ewWaitUntilTerminated, Code);
end;

// "Start with Windows" may have been switched on later in the settings app;
// remove it if it points at this copy.
procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var
  Cmd: String;
begin
  if CurUninstallStep <> usPostUninstall then exit;
  if RegQueryStringValue(HKCU, RunKey, 'Bunbun', Cmd) and (Pos(Lowercase(ExpandConstant('{app}')), Lowercase(Cmd)) > 0) then
    RegDeleteValue(HKCU, RunKey, 'Bunbun');
end;
