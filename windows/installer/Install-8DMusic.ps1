<#
.SYNOPSIS
    Registers the 8D Music APO against every render endpoint on this machine.

.DESCRIPTION
    Three things have to be true before Windows will run our effect:

      1. The COM class is registered machine-wide, so audiodg can create it.
      2. Our CLSID appears in the EFX slot of each endpoint's FxProperties.
      3. APO signature checking is off, because we ship unsigned.

    See windows/docs/APO-RESEARCH.md for why each of those is what it is, and
    what was actually verified rather than assumed.

    THE IMPORTANT PART: Windows 11 endpoints use CompositeFX properties, which
    are REG_MULTI_SZ *chains* of CLSIDs, not a single CLSID.  Overwriting one
    would silently delete the OEM's effects -- on the dev machine, Realtek's
    and Microsoft's.  So we append, and we save the exact prior value first so
    the uninstaller can put it back byte for byte.

.PARAMETER Uninstall
    Reverse everything, using the saved backup.

.PARAMETER WhatIf
    Show what would change without changing it.
#>
[CmdletBinding(SupportsShouldProcess = $true)]
param(
    [switch]$Uninstall,
    [string]$DllPath = (Join-Path $PSScriptRoot '8DMusicAPO.dll'),
    [switch]$SkipSignatureBypass,

    # Lets the same script install the path-proving passthrough probe, so the
    # registration procedure under test is the real one rather than a
    # near-miss written for the occasion.
    #   -DllPath ..\8DMusicPassthrough.dll -Clsid '{1B7C4D22-9A03-4F51-B6E8-2C90A7F30E11}'
    [string]$Clsid = '{7F9E2C10-4E2B-4C77-9E3E-8D0C1B5A6D01}',

    # Restrict to one endpoint. Used when bringing the APO up for the first
    # time: registering against a single non-default output keeps the blast
    # radius to one device instead of every output on the machine.
    [string]$EndpointId = ''
)

$ErrorActionPreference = 'Stop'

# Defaults to CLSID_EightDApoMFX in windows/src/apo/EightDApo.h. (The symbol still
# says MFX; the slot it is registered into is EFX -- see the note below.)
$CLSID      = $Clsid
$FriendlyName = '8D Music spatial engine'

$MMDevices  = 'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\MMDevices\Audio\Render'
$AudioKey   = 'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Audio'
$ClassesKey = 'HKLM:\SOFTWARE\Classes\CLSID'
$BackupDir  = Join-Path $env:ProgramData '8DMusic'
$BackupFile = Join-Path $BackupDir 'apo-registration-backup.json'
$StateFile  = Join-Path $BackupDir 'state.bin'

# System-effects property set.  See APO-RESEARCH.md section 2.
$FX = '{d04e05a6-594b-4fb6-a80d-01af5eed7d1d}'

# EFX, not MFX -- and this was traced, not guessed.
#
# An ETW capture of the audio engine starting a stream shows it initialising the
# SFX (,13) and all three EFX (,15) chain members, and *never once* touching the
# MFX chain -- not ours, and not Realtek's own two entries either. The MFX slot
# is simply not exercised on the endpoints checked. See APO-RESEARCH.md Part 2.
#
# EFX is also the right slot on the merits: it runs after all mixing, which is
# what "apply to everything, together" actually means.
$PID_EFX           = 7     # REG_SZ,       single CLSID   (Windows 10 style)
$PID_COMPOSITE_EFX = 15    # REG_MULTI_SZ, chain          (Windows 11 style)

# ---------------------------------------------------------------- registry access
#
# The endpoint keys are owned by SYSTEM. BUILTIN\Administrators is granted
# exactly "SetValue, ReadKey" -- notably NOT CreateSubKey. PowerShell's registry
# provider (New-ItemProperty, Remove-ItemProperty, Set-ItemProperty) opens a key
# for full write access, which includes CreateSubKey, so every one of them fails
# with "Requested registry access is not allowed" even in an elevated shell.
#
# The rights we actually need are just SetValue. Asking for only that succeeds
# as Administrator, so there is no need to take ownership of a system key or
# rewrite its DACL -- which would be a far more invasive thing to do to somebody's
# machine, and much harder to undo correctly.
$MMDevicesSub = 'SOFTWARE\Microsoft\Windows\CurrentVersion\MMDevices\Audio\Render'

function Open-FxKeyForWrite {
    param([string]$EndpointGuid)
    $sub = "$MMDevicesSub\$EndpointGuid\FxProperties"
    [Microsoft.Win32.Registry]::LocalMachine.OpenSubKey(
        $sub,
        [Microsoft.Win32.RegistryKeyPermissionCheck]::ReadWriteSubTree,
        [System.Security.AccessControl.RegistryRights]::SetValue -bor
        [System.Security.AccessControl.RegistryRights]::QueryValues)
}

function Assert-Admin {
    $id = [Security.Principal.WindowsIdentity]::GetCurrent()
    $pr = New-Object Security.Principal.WindowsPrincipal($id)
    if (-not $pr.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
        throw "This must run elevated. Right-click PowerShell and Run as administrator."
    }
}

function Get-RenderEndpoints {
    # DeviceState 1 = active, 2 = disabled, 4 = not present, 8 = unplugged.
    # We register everything present so switching headphones -> speakers keeps
    # the effect, rather than silently losing it. See APO-RESEARCH.md section 5.
    Get-ChildItem $MMDevices -ErrorAction SilentlyContinue | ForEach-Object {
        $state = (Get-ItemProperty $_.PSPath -ErrorAction SilentlyContinue).DeviceState
        $props = Get-ItemProperty (Join-Path $_.PSPath 'Properties') -ErrorAction SilentlyContinue
        [PSCustomObject]@{
            Id    = $_.PSChildName
            Path  = $_.PSPath
            Name  = $props.'{a45c254e-df1c-4efd-8020-67d146a850e0},2'
            State = $state
        }
    }
}

# ------------------------------------------------- the APO declaration
#
# The step that is easy to miss entirely, and fatal without.
#
# Registering the COM class is NOT enough. Every APO the audio engine will
# actually activate is also declared here, mirroring APO_REG_PROPERTIES:
#
#   HKLM\SOFTWARE\Classes\AudioEngine\AudioProcessingObjects\{CLSID}
#
# Note it is under Classes\AudioEngine -- *not*
# CurrentVersion\AudioEngine, which does not exist on this machine and which is
# where you will look first. Without this key the engine never loads the DLL at
# all: no error, no event, DllMain never runs. 91 APOs are declared here on the
# development machine, including every one that loads.
#
# The values must agree with what the APO returns from
# GetRegistrationProperties -- the engine cross-checks, and rejects a mismatch
# just as silently.
$ApoDeclRoot = 'HKLM:\SOFTWARE\Classes\AudioEngine\AudioProcessingObjects'

function Register-ApoDeclaration {
    $key = "$ApoDeclRoot\$CLSID"
    if (-not $PSCmdlet.ShouldProcess($key, 'Declare APO')) { return }

    New-Item -Path $key -Force | Out-Null
    Set-ItemProperty $key -Name 'FriendlyName'         -Value $FriendlyName
    Set-ItemProperty $key -Name 'Copyright'            -Value '8D Music'
    Set-ItemProperty $key -Name 'MajorVersion'         -Value 1     -Type DWord
    Set-ItemProperty $key -Name 'MinorVersion'         -Value 0     -Type DWord
    # APO_FLAG_DEFAULT = SAMPLESPERFRAME | FRAMESPERSECOND | BITSPERSAMPLE
    Set-ItemProperty $key -Name 'Flags'                -Value 14    -Type DWord
    Set-ItemProperty $key -Name 'MinInputConnections'  -Value 1     -Type DWord
    Set-ItemProperty $key -Name 'MaxInputConnections'  -Value 1     -Type DWord
    Set-ItemProperty $key -Name 'MinOutputConnections' -Value 1     -Type DWord
    Set-ItemProperty $key -Name 'MaxOutputConnections' -Value 1     -Type DWord
    Set-ItemProperty $key -Name 'MaxInstances'         -Value -1    -Type DWord
    Set-ItemProperty $key -Name 'NumAPOInterfaces'     -Value 1     -Type DWord
    # IID_IAudioProcessingObject
    Set-ItemProperty $key -Name 'APOInterface0' -Value '{FD7F2B29-24D0-4B5C-B177-592C39F9CA10}'
    Write-Host "  APO declared -> $key"
}

function Unregister-ApoDeclaration {
    $key = "$ApoDeclRoot\$CLSID"
    if (Test-Path $key) {
        if ($PSCmdlet.ShouldProcess($key, 'Remove APO declaration')) {
            Remove-Item $key -Recurse -Force
            Write-Host "  APO declaration removed"
        }
    }
}

function Register-ComClass {
    if (-not (Test-Path $DllPath)) {
        throw "8DMusicAPO.dll not found at $DllPath. Build it first, or pass -DllPath."
    }
    $full = (Resolve-Path $DllPath).Path
    $key  = "$ClassesKey\$CLSID"

    if ($PSCmdlet.ShouldProcess($key, 'Register COM class')) {
        New-Item -Path "$key\InprocServer32" -Force | Out-Null
        Set-ItemProperty -Path $key -Name '(default)' -Value $FriendlyName
        Set-ItemProperty -Path "$key\InprocServer32" -Name '(default)' -Value $full
        Set-ItemProperty -Path "$key\InprocServer32" -Name 'ThreadingModel' -Value 'Both'
        Write-Host "  COM class registered -> $full"
    }
}

function Unregister-ComClass {
    $key = "$ClassesKey\$CLSID"
    if (Test-Path $key) {
        if ($PSCmdlet.ShouldProcess($key, 'Remove COM class')) {
            Remove-Item $key -Recurse -Force
            Write-Host "  COM class removed"
        }
    }
}

function New-SharedStateFile {
    # The GUI cannot create this itself: audiodg is in session 0, so the block
    # must be reachable across sessions, and a non-elevated user has no
    # SeCreateGlobalPrivilege to make a Global\ object.  So the installer -- the
    # one elevated moment we get -- creates a file both sides can map instead.
    if (-not $PSCmdlet.ShouldProcess($StateFile, 'Create shared state file')) { return }

    New-Item -ItemType Directory -Path $BackupDir -Force | Out-Null
    if (-not (Test-Path $StateFile)) {
        # Size is rounded up generously; the APO maps only sizeof(SharedState).
        [IO.File]::WriteAllBytes($StateFile, (New-Object byte[] 4096))
    }

    # audiodg runs restricted; without this it cannot open the file and the
    # effect runs on defaults forever, which looks exactly like a dead GUI.
    $sddl = 'D:(A;;GA;;;WD)(A;;GA;;;AC)S:(ML;;NW;;;LW)'
    $sd = New-Object Security.AccessControl.FileSecurity
    $sd.SetSecurityDescriptorSddlForm($sddl)
    [IO.File]::SetAccessControl($StateFile, $sd)
    Write-Host "  shared state -> $StateFile"
}

function Backup-Registration {
    param($Endpoints)
    New-Item -ItemType Directory -Path $BackupDir -Force | Out-Null

    $entries = foreach ($ep in $Endpoints) {
        $fxPath = Join-Path $ep.Path 'FxProperties'
        if (-not (Test-Path $fxPath)) { continue }
        $rk    = Get-Item $fxPath
        $names = $rk.GetValueNames()

        # GetValueKind throws on a value that is not there, and a Windows 11
        # endpoint legitimately has no legacy ",6" at all -- so asking for its
        # kind unguarded aborts the backup, and with it the uninstall path.
        # Always check the name list first.
        $efxName  = "$FX,$PID_EFX"
        $compName = "$FX,$PID_COMPOSITE_EFX"

        [PSCustomObject]@{
            Id             = $ep.Id
            Name           = $ep.Name
            HadFxKey       = $true
            EfxName        = $efxName
            EfxPresent     = ($names -contains $efxName)
            EfxValue       = if ($names -contains $efxName)  { $rk.GetValue($efxName) }  else { $null }
            EfxKind        = if ($names -contains $efxName)  { [string]$rk.GetValueKind($efxName) }  else { $null }
            CompositeName  = $compName
            CompositePresent = ($names -contains $compName)
            CompositeValue = if ($names -contains $compName) { $rk.GetValue($compName) } else { $null }
            CompositeKind  = if ($names -contains $compName) { [string]$rk.GetValueKind($compName) } else { $null }
        }
    }

    $backup = [PSCustomObject]@{
        CreatedUtc   = (Get-Date).ToUniversalTime().ToString('o')
        Clsid        = $CLSID
        HadDisableDG = (Get-ItemProperty $AudioKey -Name 'DisableProtectedAudioDG' -ErrorAction SilentlyContinue).DisableProtectedAudioDG
        Endpoints    = @($entries)
    }
    if ($PSCmdlet.ShouldProcess($BackupFile, 'Write registration backup')) {
        $backup | ConvertTo-Json -Depth 6 | Set-Content -Path $BackupFile -Encoding utf8
        Write-Host "  backup -> $BackupFile"
    }
}

function Add-ApoToEndpoint {
    param($Endpoint)

    $fxPath = Join-Path $Endpoint.Path 'FxProperties'
    if (-not (Test-Path $fxPath)) {
        Write-Warning "  [$($Endpoint.Name)] has no FxProperties key - skipped."
        Write-Warning "     Creating one needs CreateSubKey, which Administrators are not granted."
        return $false
    }

    $key = Open-FxKeyForWrite $Endpoint.Id
    if (-not $key) {
        Write-Warning "  [$($Endpoint.Name)] could not open FxProperties for write - skipped."
        return $false
    }

    try {
        $names = $key.GetValueNames()
        $compositeName = "$FX,$PID_COMPOSITE_EFX"
        $legacyName    = "$FX,$PID_EFX"

        if ($names -contains $compositeName) {
            # Windows 11 chain form. Append; never replace -- the existing
            # entries are the OEM's effects and removing them is not ours to do.
            $chain = @($key.GetValue($compositeName))
            if ($chain -contains $CLSID) {
                Write-Host "  [$($Endpoint.Name)] already registered"
                return $false
            }
            $new = $chain + $CLSID
            if ($PSCmdlet.ShouldProcess($Endpoint.Name, 'Append to CompositeFX EFX chain')) {
                $key.SetValue($compositeName, [string[]]$new,
                              [Microsoft.Win32.RegistryValueKind]::MultiString)
                Write-Host "  [$($Endpoint.Name)] appended to EFX chain ($($chain.Count) -> $($new.Count))"
            }
            return $true
        }

        # Windows 10 single-CLSID form, or an endpoint with no MFX at all.
        $existing = if ($names -contains $legacyName) { $key.GetValue($legacyName) } else { $null }
        if ($existing -eq $CLSID) {
            Write-Host "  [$($Endpoint.Name)] already registered"
            return $false
        }
        if ($existing) {
            # Something else owns the slot. Overwriting would disable it
            # silently, so refuse and say so rather than fight over it.
            Write-Warning "  [$($Endpoint.Name)] EFX slot already held by $existing - skipped."
            return $false
        }
        if ($PSCmdlet.ShouldProcess($Endpoint.Name, 'Set EFX CLSID')) {
            $key.SetValue($legacyName, $CLSID, [Microsoft.Win32.RegistryValueKind]::String)
            Write-Host "  [$($Endpoint.Name)] EFX set"
        }
        return $true
    } finally {
        $key.Close()
    }
}

function Remove-ApoFromEndpoint {
    param($Endpoint)

    $fxPath = Join-Path $Endpoint.Path 'FxProperties'
    if (-not (Test-Path $fxPath)) { return }

    $key = Open-FxKeyForWrite $Endpoint.Id
    if (-not $key) {
        Write-Warning "  [$($Endpoint.Name)] could not open FxProperties for write."
        return
    }

    try {
        $names = $key.GetValueNames()
        $compositeName = "$FX,$PID_COMPOSITE_EFX"
        $legacyName    = "$FX,$PID_EFX"

        if ($names -contains $compositeName) {
            $chain = @($key.GetValue($compositeName))
            if ($chain -contains $CLSID) {
                $new = @($chain | Where-Object { $_ -ne $CLSID })
                if ($PSCmdlet.ShouldProcess($Endpoint.Name, 'Remove from EFX chain')) {
                    if ($new.Count -gt 0) {
                        $key.SetValue($compositeName, [string[]]$new,
                                      [Microsoft.Win32.RegistryValueKind]::MultiString)
                    } else {
                        # Deleting the value needs the same SetValue right.
                        $key.DeleteValue($compositeName, $false)
                    }
                    Write-Host "  [$($Endpoint.Name)] removed from EFX chain"
                }
            }
        }

        if (($names -contains $legacyName) -and ($key.GetValue($legacyName) -eq $CLSID)) {
            if ($PSCmdlet.ShouldProcess($Endpoint.Name, 'Clear EFX CLSID')) {
                $key.DeleteValue($legacyName, $false)
                Write-Host "  [$($Endpoint.Name)] EFX cleared"
            }
        }
    } finally {
        $key.Close()
    }
}

function Set-SignatureBypass {
    param([bool]$Enable)
    if ($Enable) {
        Write-Host ""
        Write-Warning "8D Music ships unsigned, so APO signature checking must be turned off:"
        Write-Warning "    $AudioKey\DisableProtectedAudioDG = 1"
        Write-Warning "This is machine-wide and affects ALL audio processing objects, not just ours."
        Write-Warning "Apps that require a protected audio path (some DRM playback) may refuse to"
        Write-Warning "play. The uninstaller restores whatever value was there before."
        Write-Host ""
        if ($PSCmdlet.ShouldProcess($AudioKey, 'Set DisableProtectedAudioDG=1')) {
            New-Item -Path $AudioKey -Force | Out-Null
            Set-ItemProperty -Path $AudioKey -Name 'DisableProtectedAudioDG' -Value 1 -Type DWord
            Write-Host "  signature check disabled"
        }
    } else {
        # This value is machine-wide and shared. On the development machine it
        # was already 1 before 8D Music existed, set by other audio software.
        # Removing it because *we* are being uninstalled would silently break
        # whatever else relies on it, so only ever put back what we recorded.
        if (-not (Test-Path $BackupFile)) {
            Write-Warning "  no backup found - leaving DisableProtectedAudioDG alone."
            Write-Warning "  Other audio software may depend on it. Remove it by hand if you are sure."
            return
        }

        $backup = Get-Content $BackupFile -Raw | ConvertFrom-Json
        $had = $backup.HadDisableDG

        if ($PSCmdlet.ShouldProcess($AudioKey, 'Restore DisableProtectedAudioDG')) {
            if ($null -ne $had) {
                Set-ItemProperty -Path $AudioKey -Name 'DisableProtectedAudioDG' -Value $had -Type DWord
                Write-Host "  signature check restored to its prior value ($had)"
            } else {
                # It genuinely did not exist before we ran, so ours is the one
                # to take away.
                Remove-ItemProperty -Path $AudioKey -Name 'DisableProtectedAudioDG' -ErrorAction SilentlyContinue
                Write-Host "  signature check re-enabled (the value did not exist before install)"
            }
        }
    }
}

function Restart-Audio {
    if (-not $PSCmdlet.ShouldProcess('audiosrv', 'Restart Windows Audio')) { return }
    Write-Host ""
    Write-Host "Restarting Windows Audio (audio will drop for a moment)..."
    try {
        Restart-Service -Name 'audiosrv' -Force -ErrorAction Stop
        Write-Host "  audio service restarted"
    } catch {
        Write-Warning "  could not restart audiosrv: $($_.Exception.Message)"
        Write-Warning "  reboot to pick up the change."
    }
}

# ----------------------------------------------------------------- main
Assert-Admin
$endpoints = @(Get-RenderEndpoints)
if ($EndpointId) {
    # Accept either the bare GUID or the full "{0.0.0.00000000}.{guid}" form
    # that IMMDevice::GetId returns, so ids can be pasted straight from
    # play_to_endpoint.exe.
    $bare = $EndpointId
    if ($bare -match '\}\.\{(?<g>[^}]+)\}$') { $bare = '{' + $Matches.g + '}' }
    $endpoints = @($endpoints | Where-Object { $_.Id -eq $bare })
    if (-not $endpoints.Count) { throw "no render endpoint matching $EndpointId" }
    Write-Host "Restricted to: $($endpoints[0].Name)  $($endpoints[0].Id)"
}

if ($Uninstall) {
    Write-Host "Removing 8D Music APO registration..."
    foreach ($ep in $endpoints) { Remove-ApoFromEndpoint $ep }
    Unregister-ComClass
    Unregister-ApoDeclaration
    Set-SignatureBypass -Enable:$false
    Restart-Audio
    Write-Host ""
    Write-Host "Done. If audio is still wrong, see windows/docs/RECOVERY.md."
    return
}

Write-Host "Installing 8D Music APO across $($endpoints.Count) render endpoint(s)..."
Backup-Registration -Endpoints $endpoints
Register-ComClass
Register-ApoDeclaration
New-SharedStateFile
foreach ($ep in $endpoints) { [void](Add-ApoToEndpoint $ep) }
if (-not $SkipSignatureBypass) { Set-SignatureBypass -Enable:$true }
Restart-Audio

Write-Host ""
Write-Host "Installed. Open 8DMusic.exe and turn 8D on."
Write-Host "To reverse:  .\Install-8DMusic.ps1 -Uninstall"
