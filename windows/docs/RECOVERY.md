# If 8D Music broke your audio

A faulty APO is loaded into `audiodg.exe`, which is the process that renders
all Windows audio. If ours misbehaves, **every sound on the machine can stop**,
and nothing on screen will point at 8D Music as the cause.

This page is the way back. It needs no working audio, no 8D Music, and no
network. Everything here is registry edits and a service restart.

---

## First: the one-line fix

Open **PowerShell as Administrator** and run:

```powershell
reg add "HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\Audio" /v DisableProtectedAudioDG /t REG_DWORD /d 0 /f
Restart-Service audiosrv -Force
```

Setting `DisableProtectedAudioDG` to `0` makes Windows refuse to load unsigned
APOs — including ours. The effect stops, and normal audio comes back. This does
not uninstall anything, so nothing is lost by trying it first.

Note it disables *every* unsigned APO on the machine, not only ours. If you also
run Equalizer APO or similar, they stop too until you set it back to `1`.

If audio returns, run the uninstaller properly when convenient:

```powershell
.\Install-8DMusic.ps1 -Uninstall
```

---

## If that did not work: remove the endpoint registration by hand

Our CLSID is:

```
{7F9E2C10-4E2B-4C77-9E3E-8D0C1B5A6D01}
```

Every place it can appear is under:

```
HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\MMDevices\Audio\Render\{endpoint}\FxProperties
```

in one of these two values:

| Value name | Type | Meaning |
| --- | --- | --- |
| `{d04e05a6-594b-4fb6-a80d-01af5eed7d1d},7` | REG_SZ | EFX, Windows 10 form |
| `{d04e05a6-594b-4fb6-a80d-01af5eed7d1d},15` | REG_MULTI_SZ | EFX chain, Windows 11 form |

The script below also sweeps the MFX slots (`,6` and `,14`), because earlier
builds registered there before tracing showed MFX is never used. Clearing a slot
we never wrote to is harmless; missing one we did is not.

Run this in an **elevated PowerShell**. It strips our CLSID from every endpoint
and leaves everything else alone:

```powershell
$CLSID = '{7F9E2C10-4E2B-4C77-9E3E-8D0C1B5A6D01}'
$FX    = '{d04e05a6-594b-4fb6-a80d-01af5eed7d1d}'
$root  = 'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\MMDevices\Audio\Render'

Get-ChildItem $root | ForEach-Object {
    $fx = Join-Path $_.PSPath 'FxProperties'
    if (-not (Test-Path $fx)) { return }

    # Administrators are granted SetValue but NOT CreateSubKey on these keys, so
    # the PowerShell provider (New-ItemProperty) is refused even when elevated.
    # Open the key asking only for the right we need.
    $sub = "SOFTWARE\Microsoft\Windows\CurrentVersion\MMDevices\Audio\Render\$($_.PSChildName)\FxProperties"
    $key = [Microsoft.Win32.Registry]::LocalMachine.OpenSubKey($sub,
             [Microsoft.Win32.RegistryKeyPermissionCheck]::ReadWriteSubTree,
             [System.Security.AccessControl.RegistryRights]::SetValue -bor
             [System.Security.AccessControl.RegistryRights]::QueryValues)
    if (-not $key) { return }

    # Chain forms: SFX, MFX, EFX and the two offload variants.
    foreach ($q in 13,14,15,19,20) {
        $n = "$FX,$q"
        if ($key.GetValueNames() -contains $n) {
            $chain = @($key.GetValue($n))
            if ($chain -contains $CLSID) {
                $new = @($chain | Where-Object { $_ -ne $CLSID })
                if ($new.Count) {
                    $key.SetValue($n, [string[]]$new, [Microsoft.Win32.RegistryValueKind]::MultiString)
                } else {
                    $key.DeleteValue($n, $false)
                }
                Write-Host "cleaned ,$q on $($_.PSChildName)"
            }
        }
    }

    # Single-CLSID forms: SFX, MFX, EFX.
    foreach ($q in 5,6,7) {
        $n = "$FX,$q"
        if (($key.GetValueNames() -contains $n) -and ($key.GetValue($n) -eq $CLSID)) {
            $key.DeleteValue($n, $false)
            Write-Host "cleared ,$q on $($_.PSChildName)"
        }
    }
    $key.Close()
}

Remove-Item "HKLM:\SOFTWARE\Classes\CLSID\$CLSID" -Recurse -Force -ErrorAction SilentlyContinue
Restart-Service audiosrv -Force
```

**Deliberately not touching `DisableProtectedAudioDG` here.** It is machine-wide
and shared: on the development machine it was already set to `1` before 8D Music
was ever installed, by some other audio software. Removing it because you are
uninstalling *us* would break *them*, with no hint as to why. The uninstaller
restores whatever value it recorded before it made any change; if you are doing
this by hand and you know nothing else needs it, remove it with:

```powershell
Remove-ItemProperty 'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Audio' `
  -Name DisableProtectedAudioDG -ErrorAction SilentlyContinue
```

---

## Restoring exactly what was there before

The installer saves the original values before touching anything:

```
C:\ProgramData\8DMusic\apo-registration-backup.json
```

It records, per endpoint, the prior effect-slot value and its type, plus whether
`DisableProtectedAudioDG` already existed. If you need to put a machine back
precisely as it was — particularly if OEM effects went missing — that file is
the source of truth. `Install-8DMusic.ps1 -Uninstall` reads it automatically.

---

## Nuclear option: let Windows rebuild the endpoints

If the registry is in a state you would rather not reason about, Windows will
recreate the endpoint keys from scratch:

1. Device Manager → **Sound, video and game controllers**
2. Right-click your audio device → **Uninstall device**
3. **Action → Scan for hardware changes**

This discards *all* per-endpoint effect configuration, including the OEM's, and
usually costs you the manufacturer's audio enhancements until you reinstall
their driver. It is a last resort, not a first one.

---

## Safe mode

If the machine will not boot far enough to run any of this, the audio service
does not start in Safe Mode, so the APO is never loaded. Boot to Safe Mode and
run the script from the previous section there; it only edits the registry, so
it works with audio entirely absent.

---

## Why the APO should not be able to do this

The APO is written to fail *open*: on any unexpected state — not locked, null
buffers, a format it did not agree to, an unrecognised buffer flag — it copies
input to output and gets out of the way. It never emits silence and never emits
an uninitialised buffer. See `passthrough()` in `windows/src/apo/EightDApo.cpp`.

That is the design. This page exists because a crash inside `audiodg` is not
something the crashing code gets to handle.
