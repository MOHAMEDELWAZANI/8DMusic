; 8D Music — Inno Setup script
;
; Produces one file, 8DMusic-Setup.exe: a normal wizard that elevates itself,
; installs to Program Files, appears in Add/Remove Programs, and prompts for a
; restart at the end.
;
; All the registry work is in [Code] rather than [Registry], because two parts
; of it cannot be expressed as a static table:
;
;   * every audio endpoint on the machine has to be walked, and each one's
;     existing value saved before it is replaced, so uninstall can put back
;     exactly what was there;
;   * the endpoint keys refuse the access Inno's own registry helpers ask for,
;     so those writes go through advapi32 directly. See WriteFxValue below.
;
; The logic is a port of the PowerShell installer that preceded this, which was
; verified by installing, uninstalling, and diffing the registry against a
; snapshot taken beforehand.

#define AppName        "8D Music"
#define AppVersion     "1.0.0"
#define AppPublisher   "8D Music"
#define AppExe         "8DMusic.exe"

[Setup]
AppId={{92BBF43A-551B-4D75-8A90-E6746CDFBC69}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher={#AppPublisher}
VersionInfoVersion={#AppVersion}
VersionInfoDescription={#AppName} setup

DefaultDirName={commonpf}\8DMusic
DefaultGroupName={#AppName}
DisableProgramGroupPage=yes
DisableDirPage=auto

; The effect installs into Windows' own audio system, which needs admin.
PrivilegesRequired=admin
PrivilegesRequiredOverridesAllowed=

; 64-bit install mode, so {commonpf} is "Program Files" rather than the x86
; folder and Inno's registry calls see the real HKLM rather than WOW6432Node.
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible

OutputDir=..\..\..\dist
OutputBaseFilename=8DMusic-Setup
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
SetupIconFile=..\..\res\8dmusic.ico
UninstallDisplayIcon={app}\{#AppExe}
UninstallDisplayName={#AppName}

; Windows only picks up a new audio effect properly after a restart, so the
; wizard finishes on the restart prompt rather than pretending otherwise.
AlwaysRestart=yes

; Shown before anything is installed: the security setting is not something to
; discover afterwards.
InfoBeforeFile=before.txt

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "Create a &desktop shortcut"; GroupDescription: "Shortcuts:"; Flags: unchecked

[Files]
; restartreplace/uninsrestartdelete: on a reinstall the old DLL is still loaded
; inside audiodg.exe and cannot be overwritten. Inno queues the replacement for
; the restart it is going to ask for anyway.
Source: "..\..\build\Release\8DMusicAPO.dll"; DestDir: "{app}"; Flags: ignoreversion restartreplace uninsrestartdelete
Source: "..\..\build\Release\8DMusic.exe";    DestDir: "{app}"; Flags: ignoreversion restartreplace uninsrestartdelete
Source: "README.txt";                          DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{group}\{#AppName}";            Filename: "{app}\{#AppExe}"
Name: "{group}\How 8D Music works";    Filename: "{app}\README.txt"
Name: "{group}\Uninstall {#AppName}";  Filename: "{uninstallexe}"
Name: "{autodesktop}\{#AppName}";      Filename: "{app}\{#AppExe}"; Tasks: desktopicon

[Run]
Filename: "{app}\README.txt"; Description: "Read what this changed on my computer"; Flags: postinstall shellexec skipifsilent unchecked

[Code]
const
  CLSID        = '{7F9E2C10-4E2B-4C77-9E3E-8D0C1B5A6D01}';
  FRIENDLY     = '8D Music spatial engine';
  FXSET        = '{d04e05a6-594b-4fb6-a80d-01af5eed7d1d}';
  SLOT         = 5;   { the stream-effect slot -- see the note in Install below }
  IID_APO      = '{FD7F2B29-24D0-4B5C-B177-592C39F9CA10}';

  MMDEV_ROOT   = 'SOFTWARE\Microsoft\Windows\CurrentVersion\MMDevices\Audio\Render';
  AUDIO_KEY    = 'SOFTWARE\Microsoft\Windows\CurrentVersion\Audio';
  CLASSES_KEY  = 'SOFTWARE\Classes\CLSID\' + CLSID;
  DECL_KEY     = 'SOFTWARE\Classes\AudioEngine\AudioProcessingObjects\' + CLSID;

  { Access rights. KEY_WOW64_64KEY matters because Setup itself is a 32-bit
    process: without it every one of these calls lands in WOW6432Node. }
  KEY_QUERY_VALUE = $0001;
  KEY_SET_VALUE   = $0002;
  KEY_WOW64_64KEY = $0100;
  REG_SZ_         = 1;
  ERROR_SUCCESS_  = 0;

function RegOpenKeyExW(hKey: Integer; lpSubKey: String; ulOptions, samDesired: Cardinal;
  var phkResult: Integer): Integer;
  external 'RegOpenKeyExW@advapi32.dll stdcall';
function RegSetValueExW(hKey: Integer; lpValueName: String; Reserved, dwType: Cardinal;
  lpData: String; cbData: Cardinal): Integer;
  external 'RegSetValueExW@advapi32.dll stdcall';
function RegDeleteValueW(hKey: Integer; lpValueName: String): Integer;
  external 'RegDeleteValueW@advapi32.dll stdcall';
function RegCloseKey(hKey: Integer): Integer;
  external 'RegCloseKey@advapi32.dll stdcall';

function BackupFile(): String;
begin
  Result := ExpandConstant('{commonappdata}\8DMusic\install-backup.ini');
end;

function SlotName(): String;
begin
  Result := FXSET + ',' + IntToStr(SLOT);
end;

{ ---------------------------------------------------------------------------
  Writing to an endpoint's FxProperties.

  These keys are owned by SYSTEM and grant Administrators exactly
  "SetValue, ReadKey" -- notably not CreateSubKey. Inno's RegWriteStringValue,
  like PowerShell's registry provider, opens a key for full write access, and
  is refused here even when elevated. Asking advapi32 for only the two rights
  actually needed succeeds, and avoids taking ownership of a system key.

  Reading is done with Inno's own helpers, because read access is granted.
  --------------------------------------------------------------------------- }
function OpenFxForWrite(const Endpoint: String; var Key: Integer): Boolean;
var
  Path: String;
begin
  Path := MMDEV_ROOT + '\' + Endpoint + '\FxProperties';
  Result := RegOpenKeyExW(HKEY_LOCAL_MACHINE, Path, 0,
              KEY_SET_VALUE or KEY_QUERY_VALUE or KEY_WOW64_64KEY, Key) = ERROR_SUCCESS_;
end;

function WriteFxValue(const Endpoint, ValueName, Data: String): Boolean;
var
  Key: Integer;
begin
  Result := False;
  if not OpenFxForWrite(Endpoint, Key) then Exit;
  try
    Result := RegSetValueExW(Key, ValueName, 0, REG_SZ_, Data,
                             (Length(Data) + 1) * 2) = ERROR_SUCCESS_;
  finally
    RegCloseKey(Key);
  end;
end;

function DeleteFxValue(const Endpoint, ValueName: String): Boolean;
var
  Key: Integer;
begin
  Result := False;
  if not OpenFxForWrite(Endpoint, Key) then Exit;
  try
    Result := RegDeleteValueW(Key, ValueName) = ERROR_SUCCESS_;
  finally
    RegCloseKey(Key);
  end;
end;

{ --------------------------------------------------------------------------- }

procedure CreateSettingsFile();
var
  Dir, F: String;
  Code: Integer;
begin
  { 8DMusic.exe cannot create this itself: the effect runs inside audiodg.exe
    in session 0, so the settings block has to cross sessions, and an ordinary
    account has no privilege to make a global one. Setup is the one elevated
    moment available. }
  Dir := ExpandConstant('{commonappdata}\8DMusic');
  ForceDirectories(Dir);
  F := Dir + '\state.bin';
  if not FileExists(F) then
    SaveStringToFile(F, '', False);

  { audiodg runs on a restricted token and cannot open a file with ordinary
    permissions. icacls is used rather than the ACL APIs because the equivalent
    Pascal would be a page of SID plumbing for no gain. }
  Exec(ExpandConstant('{sys}\icacls.exe'),
       '"' + F + '" /grant *S-1-1-0:(F)', '', SW_HIDE, ewWaitUntilTerminated, Code);
  Exec(ExpandConstant('{sys}\icacls.exe'),
       '"' + F + '" /setintegritylevel (NW)Low', '', SW_HIDE, ewWaitUntilTerminated, Code);
end;

procedure RegisterEffect();
var
  Dll: String;
begin
  Dll := ExpandConstant('{app}\8DMusicAPO.dll');

  { 1. the COM class }
  RegWriteStringValue(HKLM64, CLASSES_KEY, '', FRIENDLY);
  RegWriteStringValue(HKLM64, CLASSES_KEY + '\InprocServer32', '', Dll);
  RegWriteStringValue(HKLM64, CLASSES_KEY + '\InprocServer32', 'ThreadingModel', 'Both');

  { 2. the declaration.

    Registering the COM class is NOT enough. Without this key the audio engine
    never loads the DLL at all -- no error, nothing in the event log. Note the
    path is under Classes\AudioEngine, not CurrentVersion\AudioEngine, which
    does not exist. The values have to agree with what the DLL reports from
    GetRegistrationProperties, because the engine cross-checks them. }
  RegWriteStringValue(HKLM64, DECL_KEY, 'FriendlyName', FRIENDLY);
  RegWriteStringValue(HKLM64, DECL_KEY, 'Copyright', '8D Music');
  RegWriteDWordValue (HKLM64, DECL_KEY, 'MajorVersion', 1);
  RegWriteDWordValue (HKLM64, DECL_KEY, 'MinorVersion', 0);
  RegWriteDWordValue (HKLM64, DECL_KEY, 'Flags', 14);
  RegWriteDWordValue (HKLM64, DECL_KEY, 'MinInputConnections', 1);
  RegWriteDWordValue (HKLM64, DECL_KEY, 'MaxInputConnections', 1);
  RegWriteDWordValue (HKLM64, DECL_KEY, 'MinOutputConnections', 1);
  RegWriteDWordValue (HKLM64, DECL_KEY, 'MaxOutputConnections', 1);
  RegWriteDWordValue (HKLM64, DECL_KEY, 'MaxInstances', $FFFFFFFF);
  RegWriteDWordValue (HKLM64, DECL_KEY, 'NumAPOInterfaces', 1);
  RegWriteStringValue(HKLM64, DECL_KEY, 'APOInterface0', IID_APO);
end;

procedure ConfigureEndpoints();
var
  Names: TArrayOfString;
  I, Saved: Integer;
  Ep, Prev, VName: String;
  Had: Boolean;
  Ini: String;
begin
  Ini := BackupFile();
  VName := SlotName();
  Saved := 0;

  if not RegGetSubkeyNames(HKLM64, MMDEV_ROOT, Names) then Exit;

  for I := 0 to GetArrayLength(Names) - 1 do
  begin
    Ep := Names[I];

    { Reading is allowed, so Inno's own helper is fine here. }
    Had := RegQueryStringValue(HKLM64, MMDEV_ROOT + '\' + Ep + '\FxProperties',
                               VName, Prev);

    { Our own CLSID already sitting there is a leftover from a previous
      install, not a value worth restoring. Record it as "there was nothing
      here" so uninstall clears the slot rather than putting us back. }
    if Had and (CompareText(Prev, CLSID) = 0) then
    begin
      Had := False;
      Prev := '';
    end;

    { Somebody else's effect owns the slot. Overwriting it would silently
      disable their software, so leave it and record that we did not touch it. }
    if Had and (Prev <> '') then
    begin
      SetIniString('Endpoints', Ep + '.touched', '0', Ini);
      Continue;
    end;

    if not WriteFxValue(Ep, VName, CLSID) then
    begin
      { No FxProperties key at all, or it would not open. Creating one needs
        CreateSubKey, which Administrators are not granted, so this device
        simply cannot carry an effect. }
      SetIniString('Endpoints', Ep + '.touched', '0', Ini);
      Continue;
    end;

    SetIniString('Endpoints', Ep + '.touched', '1', Ini);
    if Had then
      SetIniString('Endpoints', Ep + '.prev', Prev, Ini)
    else
      SetIniString('Endpoints', Ep + '.prev', '', Ini);
    Saved := Saved + 1;
  end;

  SetIniString('General', 'valueName', VName, Ini);
  SetIniInt('General', 'count', Saved, Ini);
end;

procedure DisableSignatureCheck();
var
  Existing: Cardinal;
  Ini: String;
begin
  Ini := BackupFile();

  { Machine-wide and shared. Record what was there so uninstall can put back
    the same value rather than breaking other audio software that may rely on
    it having been off already. }
  if RegQueryDWordValue(HKLM64, AUDIO_KEY, 'DisableProtectedAudioDG', Existing) then
    SetIniString('General', 'hadDisableDG', IntToStr(Existing), Ini)
  else
    SetIniString('General', 'hadDisableDG', 'absent', Ini);

  RegWriteDWordValue(HKLM64, AUDIO_KEY, 'DisableProtectedAudioDG', 1);
end;

procedure RestartAudio();
var
  Code: Integer;
begin
  { A restart is coming anyway, but bouncing the service means the effect is
    often live before then. }
  Exec(ExpandConstant('{sys}\net.exe'), 'stop audiosrv /y', '', SW_HIDE,
       ewWaitUntilTerminated, Code);
  Exec(ExpandConstant('{sys}\net.exe'), 'start audiosrv', '', SW_HIDE,
       ewWaitUntilTerminated, Code);
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if CurStep = ssPostInstall then
  begin
    { Order matters: the backup file has to exist before anything is changed,
      and the endpoints are written last because they are what actually turns
      the effect on. }
    ForceDirectories(ExpandConstant('{commonappdata}\8DMusic'));
    RegisterEffect();
    CreateSettingsFile();
    DisableSignatureCheck();
    ConfigureEndpoints();
    RestartAudio();
  end;
end;

{ ---------------------------------------------------------------- uninstall }

procedure RestoreEndpoints();
var
  Names: TArrayOfString;
  I: Integer;
  Ep, Prev, VName, Cur: String;
  Ini: String;
begin
  Ini := BackupFile();
  VName := GetIniString('General', 'valueName', SlotName(), Ini);

  if not RegGetSubkeyNames(HKLM64, MMDEV_ROOT, Names) then Exit;

  for I := 0 to GetArrayLength(Names) - 1 do
  begin
    Ep := Names[I];

    { Only undo what is still ours. If something else has taken the slot since,
      it is no longer our value to put back. }
    if not RegQueryStringValue(HKLM64, MMDEV_ROOT + '\' + Ep + '\FxProperties',
                               VName, Cur) then
      Continue;
    if CompareText(Cur, CLSID) <> 0 then
      Continue;

    Prev := GetIniString('Endpoints', Ep + '.prev', '', Ini);
    if Prev <> '' then
      WriteFxValue(Ep, VName, Prev)
    else
      DeleteFxValue(Ep, VName);
  end;
end;

procedure RestoreSignatureCheck();
var
  Had: String;
  Ini: String;
begin
  Ini := BackupFile();
  Had := GetIniString('General', 'hadDisableDG', '', Ini);

  if Had = 'absent' then
    RegDeleteValue(HKLM64, AUDIO_KEY, 'DisableProtectedAudioDG')
  else if Had <> '' then
    RegWriteDWordValue(HKLM64, AUDIO_KEY, 'DisableProtectedAudioDG', StrToInt(Had))
  else
    { No record of what it was -- leave it rather than guess, because other
      audio software may depend on it. }
    ;
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  if CurUninstallStep = usUninstall then
  begin
    RestoreEndpoints();
    RestoreSignatureCheck();
    RegDeleteKeyIncludingSubkeys(HKLM64, DECL_KEY);
    RegDeleteKeyIncludingSubkeys(HKLM64, CLASSES_KEY);
    RestartAudio();
    DeleteFile(ExpandConstant('{commonappdata}\8DMusic\state.bin'));
  end;
end;
