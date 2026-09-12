# IT WORKS. The recipe, in full.

Verified on Windows 11 Pro 10.0.22631: `8DMusicAPO.dll` loads into `audiodg.exe`,
processes system audio, and the effect is audible. The control window reports
`PROCESSING · 48000 Hz, 2 ch`, the heartbeat advances ~103 buffers/second, the
orbit angle moves, and peakL/peakR differ and swap.

**Five things must all be true. Miss any one and Windows skips the APO in
complete silence -- no error, no event log entry, nothing in an ETW trace. That
silence is the whole difficulty; it is indistinguishable from "Windows refuses
third-party APOs", which is not what is happening.**

### 1. The COM class

```
HKLM\SOFTWARE\Classes\CLSID\{clsid}\InprocServer32
    (default)      = C:\Program Files\8DMusic\8DMusicAPO.dll   REG_SZ
    ThreadingModel = Both
```

### 2. The APO declaration -- the one everybody misses

```
HKLM\SOFTWARE\Classes\AudioEngine\AudioProcessingObjects\{clsid}
    FriendlyName         REG_SZ    8D Music spatial engine
    Copyright            REG_SZ    8D Music
    MajorVersion         DWORD     1
    MinorVersion         DWORD     0
    Flags                DWORD     14        (APO_FLAG_DEFAULT)
    MinInputConnections  DWORD     1
    MaxInputConnections  DWORD     1
    MinOutputConnections DWORD     1
    MaxOutputConnections DWORD     1
    MaxInstances         DWORD     -1        (unlimited)
    NumAPOInterfaces     DWORD     1
    APOInterface0        REG_SZ    {FD7F2B29-24D0-4B5C-B177-592C39F9CA10}
```

That last GUID is `IID_IAudioProcessingObject`.

Note the path: **`Classes\AudioEngine`**, not `CurrentVersion\AudioEngine`.
The latter does not exist on this machine and is where you will look first.
Without this key the engine never even calls `LoadLibrary` -- `DllMain` does not
run. 91 APOs are declared here, including every one that loads.

The values must agree with what `GetRegistrationProperties` returns. The engine
cross-checks, and rejects a mismatch as silently as everything else -- including
checking that the CLSID the APO *reports* is the CLSID it was *activated as*.

### 3. The endpoint slot

```
HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\MMDevices\Audio\Render\{endpoint}\FxProperties
    {d04e05a6-594b-4fb6-a80d-01af5eed7d1d},5 = {clsid}    REG_SZ   SFX
    {d04e05a6-594b-4fb6-a80d-01af5eed7d1d},7 = {clsid}    REG_SZ   EFX
```

The **legacy single-CLSID** form, which is what Equalizer APO uses and what
works. Writing it needs the `SetValue`-only trick from Part 2 -- Administrators
are not granted `CreateSubKey` on these keys, so PowerShell's provider is
refused even when elevated.

### 4. The object must support COM aggregation

The engine calls `IClassFactory::CreateInstance` with a **non-null controlling
unknown** and `IID_IUnknown`. `if (outer) return CLASS_E_NOAGGREGATION;` -- the
reflexive thing to write, and what this project shipped for weeks -- makes the
engine walk away without a word. See `EightDApo` for the delegating/
non-delegating split.

### 5. The interfaces the engine actually asks for

* `IAudioProcessingObject`, `...Configuration`, `...RT`
* `IAudioSystemEffects3` (which covers 2 and 1). Windows 11 queries it by IID
  and gives up without it. Claiming it also changes `Initialize`'s `cbDataSize`
  from 88 to 80.
* `IAgileObject`, and `IMarshal` via an aggregated free-threaded marshaler
  (`CoCreateFreeThreadedMarshaler`). With `ThreadingModel=Both` the engine calls
  from any apartment; refusing these sends it into a long round of fruitless
  marshalling probes.

Then restart `audiosrv`. A reboot is **not** required -- that was tested
separately and made no difference either way.

### How this was found

By instrumenting the probe to log every COM entry point to a file under
`%SystemRoot%\Temp` (audiodg's restricted token cannot write to `ProgramData`),
then borrowing Equalizer APO's working CLSID so our code was reached at all. The
`outer=` pointer in the `CreateInstance` log line is what broke it open.

---

# Windows APO registration: what is actually true in 2026

Written while bringing up the Windows build. Everything marked **verified** was
observed on the development machine (Windows 11 Pro 10.0.22631) or read out of
its registry. Everything marked **from documentation** is sourced but not yet
reproduced here. Nothing in this file is from recollection.

The point of this document is that the next person does not repeat the
research. Where a claim is unverified, it says so.

---

## 1. Which effect slot: SFX, MFX or EFX

| Slot | Runs | Scope | May change channel count |
| --- | --- | --- | --- |
| SFX (stream) | before the mixer | one per application stream | yes — the only slot that may |
| MFX (mode) | after the mix, per processing mode | all streams sharing a mode | no |
| EFX (endpoint) | after all mixing and SRC, before `CAudioLimiter` | the whole device | no |

**8D Music wants MFX.** The requirement is "everything, mixed together":
Spotify and Discord and a game must orbit as one scene, not as three
independent orbits. That rules out SFX, which would instantiate the effect once
per stream and spatialise each app separately.

MFX rather than EFX because EFX sits after sample-rate conversion at the very
end of the chain and is the slot most likely to be handed a non-stereo or
device-native format. MFX runs in the engine's mix format, which on every
endpoint checked here is 32-bit float — the format the DSP already wants.

The trade-off to record: MFX is per *processing mode*. A stream in a different
mode (raw, communications, movie) goes through a different MFX instance or
none. `PKEY_MFX_ProcessingModes_Supported_For_Streaming` controls which modes
we are offered.

---

## 2. How an APO is bound to an endpoint

Two registrations are required. Doing only the first is the most common reason
an APO "installs" and never runs.

### 2a. The COM class — machine-wide

```
HKLM\SOFTWARE\Classes\CLSID\{our-clsid}\InprocServer32
    (default)      = full path to 8DMusicAPO.dll
    ThreadingModel = Both
```

### 2b. The endpoint binding — per device

```
HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\MMDevices\Audio\Render\{endpoint}\FxProperties
```

Property names are `{fmtid},pid`. The system-effects fmtid is
`{d04e05a6-594b-4fb6-a80d-01af5eed7d1d}`.

| pid | Meaning | Type |
| --- | --- | --- |
| 0 | `PKEY_FX_Association` (KSNODETYPE) | REG_SZ |
| 1, 2 | LFX / GFX — legacy, pre-Win8 | REG_SZ |
| 5, 6, 7 | SFX / MFX / EFX, **single** CLSID | REG_SZ |
| 13, 14, 15 | CompositeFX SFX / MFX / EFX, **chain** | REG_MULTI_SZ |
| 19, 20 | CompositeFX offload SFX / MFX | REG_MULTI_SZ |

### The finding that matters — verified on this machine

Windows 11 endpoints do not necessarily use `,5`/`,6`/`,7` at all.

The active output on the dev box (`{8d713110-…}`, `DeviceState=1`) carries
**only** the CompositeFX properties, and they are `REG_MULTI_SZ` lists holding
several CLSIDs each — effect *chains*, not single effects:

```
{d04e05a6-…},0    REG_SZ          {00000000-0000-0000-0000-000000000000}
{d04e05a6-…},13   REG_MULTI_SZ    {905069CC-…}
{d04e05a6-…},14   REG_MULTI_SZ    {90609662-…}  {2A6CD79F-…}
{d04e05a6-…},15   REG_MULTI_SZ    {8F3540DF-…}  {90705486-…}  {0BDC9AB6-…}
{d04e05a6-…},19   REG_MULTI_SZ    {90B02B1F-…}
{d04e05a6-…},20   REG_MULTI_SZ    {90C0662B-…}  {8F3540DF-…}
```

There is no `,5`, `,6` or `,7` on that endpoint.

Meanwhile the Realtek endpoints on the same machine use the **old** form:

```
{d04e05a6-…},5 = {C9453E73-…}      single CLSID, REG_SZ
{d04e05a6-…},6 = {13AB3EBD-…}
{d04e05a6-…},3 = {5860E1C5-…}
```

**Therefore the installer must handle both shapes, per endpoint, and must
decide which by inspecting what is already there — not by Windows version.**
Both forms exist simultaneously on one machine.

**Consequence for our installer:** where CompositeFX is in use, we *append* our
CLSID to the `,14` (MFX) chain rather than replacing it. Overwriting would
silently delete the OEM's effects — on this machine, Realtek's and Microsoft's.
That is a destructive change to someone's audio that we would not be able to
undo without having recorded the original. **The uninstaller must restore the
exact prior value, so the installer has to save it first.**

Equalizer APO's classic approach — *replace* the CLSID and chain to the
previous one via `PKEY_FX_...` child APO keys — predates CompositeFX. Do not
copy it verbatim; the chain form makes appending both easier and safer.

### Unverified

* Precedence when both `,6` and `,14` are present. Untested.
* Whether writing `,6` on a CompositeFX endpoint is honoured at all.

Both must be settled by experiment before the installer ships.

---

## 3. Signing — the cost question, answered

**An unsigned APO can load. No EV certificate is required.**

Windows verifies APO signatures before loading them into `audiodg.exe`. The
check is disabled machine-wide by:

```
HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\Audio
    DisableProtectedAudioDG = 1   (REG_DWORD)
```

This is exactly what Equalizer APO's installer does, and why it has worked
unsigned for over a decade. Removing the value restores the check.

This is the single most important finding for scheduling: **the worst case in
the brief — "if the answer is a signed driver package, that is a cost decision
(EV certificate)" — does not apply.** There is no certificate purchase on the
critical path.

It is not free of consequence, and the installer must say so plainly:

* It weakens a machine-wide security control, for all APOs, not just ours.
* Documented side effect: applications that require a protected audio path
  (some DRM-protected playback) may change behaviour or refuse to output audio.

**Recommendation:** ask for consent explicitly at install time and state both
effects. Do not set it silently. The uninstaller must remove the value —
restoring it to *absent*, not to `0`, unless it was already present, in which
case its prior value must be restored.

### Unverified

Whether Secure Boot, Memory Integrity / HVCI, or S-mode override
`DisableProtectedAudioDG`. Must be tested on a machine with HVCI on.

---

## 4. What restart is required

The registry entries are read by the Windows Audio service. Restarting the
service is normally enough; a reboot is not required.

```
Restart-Service -Name audiosrv -Force          # takes AudioEndpointBuilder too
```

`audiodg.exe` is started on demand by the service and picks up the new chain
when it next starts. **From documentation; not yet reproduced here.**

Expect audio to drop for a second or two, and expect applications holding an
exclusive-mode stream to need restarting.

---

## 5. When the endpoint changes

Registration is per endpoint GUID, so it does not follow the user. Headphones,
speakers, a USB DAC and a Bluetooth headset are four separate endpoints, and a
newly connected device has no registration at all.

The dev machine has **29 render endpoints**, most of them `DeviceState=4`
(unplugged) — the scale of the problem is real, not hypothetical.

Options, in order of preference:

1. Register on every present render endpoint at install time, and have
   `8DMusic.exe` watch for device arrival (`IMMNotificationClient`) and register
   newcomers. Costs a background component.
2. Register only the current default and re-register when the default changes.
   Cheaper, but the effect vanishes when the user switches device — which reads
   as a bug.

Option 1 is what the product requirement ("their selected output stays
selected") actually implies.


---

## 6. What format the APO receives

**From documentation, and consistent with what the endpoints here advertise:**
the engine mix format for MFX is 32-bit IEEE float. Channel count is the
endpoint mix channel count — **not guaranteed stereo.** A 5.1 or 7.1 endpoint
gives 6 or 8 channels. Sample rate is whatever the endpoint mix format says:
44.1, 48, 96 or 192 kHz.

The DSP is stereo float. The decision, stated explicitly as the brief requires:

* **32-bit float, 2 channels** — accept, at any sample rate. `Processor::init`
  takes the rate, so 44.1/48/96/192 all work.
* **Anything else** — decline from `IsInputFormatSupported` with
  `APOERR_FORMAT_NOT_SUPPORTED`.

Declining is the honest answer rather than silently passing through: it lets
the engine pick a format we can handle, and it means we never claim to be
processing audio we are not. What we must *not* do is accept a 5.1 buffer and
read it as stereo — that is the "declining silently is not acceptable" case,
and it would produce garbage.

The GUI must surface this. If the selected endpoint is 5.1, the user needs to
be told the effect is inactive **and why**, not left looking at a `WAITING`
indicator with no explanation.

Multichannel downmix is a deliberate non-goal for the first release. It is a
real feature, not a bug fix, and it changes what the effect is.

---

## 7. Safety notes

A faulty APO takes `audiodg.exe` down and the user loses all system audio, with
nothing pointing at us.

* Windows does disable an APO that repeatedly fails to load, but do not rely on
  it. **Unverified** — the exact retry count and behaviour were not established.
* The APO must fail *open*: on any unexpected state, copy input to output.
  Never emit silence, and never emit an uninitialised buffer.
* The uninstaller must work when audio is already broken — it touches only the
  registry and must not need `audiodg` alive.
* Ship the manual recovery in writing. See `windows/docs/RECOVERY.md`.

---

## Sources

* [dechamps/APO — notes on Windows Audio Processing Objects](https://github.com/dechamps/APO)
* [Windows 11 APIs for Audio Processing Objects — Microsoft Learn](https://learn.microsoft.com/en-us/windows-hardware/drivers/audio/windows-11-apis-for-audio-processing-objects)
* [Implementing Audio Processing Objects — Microsoft Learn](https://learn.microsoft.com/en-us/windows-hardware/drivers/audio/implementing-audio-processing-objects)
* [Equalizer APO — developer documentation](https://sourceforge.net/p/equalizerapo/wiki/Developer%20documentation/)
* Live registry of the development machine, Windows 11 Pro 10.0.22631

---

# Part 2: what happened when we actually tried it

Everything above section 7 was research. This part is measurement, on Windows 11
Pro 10.0.22631, using the trivial passthrough APO (`PassthroughApo.cpp`) so the
DSP could not be the variable.

**Outcome: the APO never loaded.** Audio kept playing throughout, which is the
fail-open behaviour working. What follows is what was ruled out, with evidence,
so the next person starts where this stopped rather than at the beginning.

## Corrections to Part 1

### Administrators cannot write FxProperties the obvious way

`New-ItemProperty` / `Set-ItemProperty` on an endpoint's `FxProperties` fails
with **"Requested registry access is not allowed"** even in an elevated shell.

The key is owned by `NT AUTHORITY\SYSTEM`, and `BUILTIN\Administrators` is
granted exactly:

```
BUILTIN\Administrators    SetValue, ReadKey
```

— notably **not** `CreateSubKey`. PowerShell's registry provider opens a key for
full write access, which includes `CreateSubKey`, so it is refused.

The fix is not to take ownership or rewrite the DACL of a system key. Open the
key asking for only the right actually needed:

```powershell
[Microsoft.Win32.Registry]::LocalMachine.OpenSubKey($sub,
    [Microsoft.Win32.RegistryKeyPermissionCheck]::ReadWriteSubTree,
    [System.Security.AccessControl.RegistryRights]::SetValue -bor
    [System.Security.AccessControl.RegistryRights]::QueryValues)
```

That succeeds as Administrator. `Install-8DMusic.ps1` does this.

A consequence: an endpoint with **no** `FxProperties` key at all cannot be given
one, because creating it needs `CreateSubKey`. The installer skips those and
says so.

### MFX is the wrong slot — it is not used at all

Part 1 recommended MFX. That was wrong, and an ETW trace of the audio engine
says so plainly.

Tracing provider `{AE4BD3BE-F36F-45B6-8D21-BDD6FB832853}` while a shared-mode
stream started on the endpoint, the engine logged `System_Effect_APO_Initialized`
for exactly these CLSIDs:

```
{905069cc-...}   Realtek SFX   -- the ,13 chain
{8f3540df-...}   EFX           -- the ,15 chain
{90705486-...}   EFX           -- the ,15 chain
{0bdc9ab6-...}   EFX           -- the ,15 chain
{5bbc2c71-...}   AdaptiveSpatialAudioRenderer, from audioeng.dll
```

The MFX chain members — Realtek's own `{90609662-...}` and `{2a6cd79f-...}` —
appear **zero times** in the entire trace. So does ours. The MFX slot is simply
not exercised on this endpoint, for anybody.

**If you are choosing a slot, choose EFX.** It is also the right slot on the
merits: it runs after all mixing, which is what "everything, mixed together"
means.

## What was ruled out, and how

| Hypothesis | Verdict | Evidence |
| --- | --- | --- |
| Unsigned APOs are blocked | **No** | `DisableProtectedAudioDG` was already `1` on this machine. `audiodg.exe` module list is enumerable as Administrator (52-53 modules), so it is **not** running as a protected process. |
| COM class registered wrong | **No** | `[Activator]::CreateInstance([Type]::GetTypeFromCLSID(...))` on our CLSID **succeeds** and returns a live object. |
| Registration structurally different from a working APO | **No** | Realtek's registration is identical in shape: `CLSID\{guid}\InprocServer32` = path, `ThreadingModel` = `Both`. Nothing else. There is no `AudioEngine\AudioProcessingObjects` key on this machine at all, and the working APOs are not in one. |
| DLL in a user profile directory audiodg cannot read | **No** | Moved to `C:\Program Files\8DMusic\`. No change. |
| Missing `IAudioSystemEffects2` | **No** | Implemented it (`GetEffectsList` returning an empty list). No change. |
| Dynamic CRT dependency audiodg cannot satisfy | **No** | Our DLL imported `VCRUNTIME140.dll`, `VCRUNTIME140_1.dll` and the UCRT forwarders; Realtek's imports only system DLLs. Rebuilt with the static CRT so imports are now `ole32`/`ADVAPI32`/`KERNEL32` only. No change. **Keep the static CRT anyway** — see the note in CMakeLists.txt; it also removes any need for users to install the VC++ redistributable. |
| Windows rewrites the chain and strips our entry | **No** | Our CLSID is still present in the registry after `audiosrv` restarts and after playback. |
| Wrong slot | **Partly** — MFX is definitely not used, but appending to the EFX chain (`,15`) did not load it either. |

Registration was tried three ways, all persisted in the registry, none
instantiated:

* `,14` CompositeFX MFX chain, appended
* `,6` legacy single-CLSID MFX
* `,15` CompositeFX EFX chain, appended

## Round two: four more hypotheses, all dead

Tested after the first write-up, each changing one variable.

| Hypothesis | Verdict | Evidence |
| --- | --- | --- |
| Working APOs have a `CLSID\{guid}\AudioProcessingObject\Properties` subkey we lack | **No** | A full recursive `reg export` of four working APOs (`RtkAPOMFX2`, `FMAPORENMFX`, `RtkAPOSFX2`, `SEAPOEFX`) shows the entire registration is the CLSID key plus `InProcServer32` holding only `@` and `ThreadingModel`. No extra subkeys, no extra values. Identical in shape to ours. |
| Wrong slot — SFX untried | **No** | The SFX chain (`,13`) is demonstrably live (Realtek's SFX initialises there in the trace). Appending ours: not loaded. |
| The DLL must live in System32 / be `REG_EXPAND_SZ` | **No** | Every APO that loads here is under `%SystemRoot%\System32` and registered `REG_EXPAND_SZ`; ours was `REG_SZ` under Program Files. Copied to System32, re-registered as `REG_EXPAND_SZ %SystemRoot%\System32\...`, verified `CoCreateInstance` still succeeds: not loaded. |
| Windows ignores *additions* to a chain but honours the original entry | **No** | Made ours the **sole** occupant of the SFX slot, Realtek's entry removed. Not loaded. So it is not about appending. |
| Unsigned DLLs are refused despite `DisableProtectedAudioDG=1` | **No** | Signed with a self-signed cert trusted in Root + TrustedPublisher; `Get-AuthenticodeSignature` reported `Valid`. Not loaded. The certificate was removed from every store afterwards. |

So: identical registration shape, identical directory, identical value type,
a valid signature, sole occupancy of a slot known to be used — and the engine
still never instantiates it, and still logs nothing.

**Do not re-run any of the above.** The variables that remain are inside the
DLL's COM behaviour or in some endpoint-level declaration not yet found, not in
where the file sits or how it is registered.

## Round three: the base class, and what the working APOs have in common

**Full paths and signers of every APO loaded in audiodg on this machine:**

| DLL | Path | Signer |
| --- | --- | --- |
| FMAPO64, SEAPO64, SECOMN64, SEHDHF64, SEHDRA64, fmaudvb | plain `C:\Windows\System32\` | Microsoft Windows Hardware Compatibility Publisher |
| RltkAPOU642, RtkUApo2Api | `System32\DriverStore\FileRepository
ealtekuapo2.inf_...` | Microsoft Windows Hardware Compatibility Publisher |

So they are **not** all in the DriverStore — the path does not distinguish them.
What every one of them shares is the WHQL signature, i.e. they all arrived as
part of a signed driver package.

That suggested the engine might require a WHQL-signed package rather than merely
a signed DLL. **Equalizer APO argues against it**: its `EqualizerAPO.dll` is
`NotSigned` outright, and it also ships and depends on the dynamic CRT
(`MSVCP140`, `VCRUNTIME140`) — so neither WHQL nor a static CRT can be strictly
required, *provided* Equalizer APO actually loads on this Windows build. That is
the one experiment still outstanding and it is the gate for everything else.

**The other difference, which did point somewhere:** every APO that loads here
imports `audioeng.dll`, and so does `EqualizerAPO.dll`. Ours imported only
ole32/advapi32/kernel32, because it hand-rolled the COM object from the raw
interfaces instead of deriving from Microsoft's `CBaseAudioProcessingObject`.

The probe now derives from that base class. For anyone rebuilding this, the
link line is not obvious:

* `audiobaseprocessingobject.lib` — `CBaseAudioProcessingObject` itself
* `audiomediatypecrt.lib` — `CreateAudioMediaTypeFromUncompressedAudioFormat`,
  which the base class calls
* `audioeng.lib` — `AERT_Allocate` / `AERT_Free`
* and the **ATL component** of the build tools, because `audiomediatypecrt.lib`
  pulls in `atls.lib`. It is not in the default C++ build tools install; add
  `Microsoft.VisualStudio.Component.VC.ATL`.

**It still does not load.** Deriving from the base class changed nothing. Note
the rebuilt DLL still does not *import* `audioeng.dll`, because the base class
comes from a static library and the linker only emits an import for symbols
actually referenced — so this test does not fully reproduce the imports of a
working APO, and that residual difference is unexplained.

## Round four: Equalizer APO installed, and the problem finally cornered

Equalizer APO was installed on the Headphones (Realtek) endpoint and the machine
rebooted. **It loads.** `EqualizerAPO.dll` appears in `audiodg`, and so does
`ClownfshAPO64.dll` from unrelated software. So third-party APOs work on this
machine, and "Windows refuses third-party APOs" is dead as a theory.

**How Equalizer APO registers — the thing worth copying:**

```
{d04e05a6-...},5 = [REG_SZ] {EACD2258-FCAC-4FF4-B36D-419E924A6D79}   SFX  (Pre-Mix)
{d04e05a6-...},7 = [REG_SZ] {EC1CC9CE-FAED-4822-828A-82A81A6F018F}   EFX  (Post-Mix)
```

Legacy **single-CLSID `REG_SZ`** properties, one CLSID per slot, two separate
CLSIDs backed by the same DLL. It does **not** touch the CompositeFX chains
(`,13`/`,14`/`,15`) at all. Its own DLL is in `C:\Program Files\EqualizerAPO\`,
is **unsigned**, and links the **dynamic** CRT — so location, signing and CRT are
all conclusively not requirements.

An ETW trace shows its SFX initialising with
`AudioSignalProcessingMode={00000000-0000-0000-0000-000000000000}` — the legacy
null mode, as opposed to the `{c18e2f7e-...}` DEFAULT mode the CompositeFX
entries use.

### The A/B that corners it

| Test | Result |
| --- | --- |
| Our CLSID in `,7` on the endpoint where EQ APO works | not loaded |
| Our CLSID in `,5` — the exact slot where EQ APO's CLSID *does* initialise | not loaded |
| A brand-new, never-used GUID in `,5` | not loaded |
| **EQ APO's own working CLSID repointed at OUR DLL** | **our DLL loaded into audiodg** |

That last row is the important one. The engine will happily load our binary —
path, ACLs, signature, CRT and imports are all fine. What it will not do is
activate a CLSID *we* put into the slot, even a fresh one, even in the slot that
demonstrably works for someone else's CLSID.

Registration was compared exhaustively and is byte-identical: same `REG_SZ`,
same absolute Program Files path, same `ThreadingModel=Both`, same default-value
name, and the CLSID keys' **ACLs match entry for entry**.

### What is left

One difference remains between Equalizer APO's successful path and every test
here: **Equalizer APO's registration was followed by a reboot.** Every test
above only restarted `audiosrv`. If the legacy `,5`/`,7` properties are consumed
when endpoints are enumerated at boot and cached thereafter, that alone would
explain a silent no-op — and it is the last cheap thing to try.

Test it by registering the probe into `,5`, rebooting, and checking `audiodg`'s
module list.

## THE ANSWER: the engine aggregates the APO

Equalizer APO was installed and does load, which killed "Windows refuses
third-party APOs". The remaining difference was found by instrumenting the probe
until the engine showed its hand -- borrowing Equalizer APO's working CLSID and
pointing it at our DLL, so our code would actually be reached.

The log:

```
DllMain: DLL_PROCESS_ATTACH
DllGetClassObject: clsid={EACD2258-...}  riid={00000001-...}   IID_IClassFactory
Factory CreateInstance: riid={00000000-0000-0000-C000-000000000046} outer=0x2847...
```

`riid` is **IID_IUnknown** and **`outer` is non-null**. The Windows audio engine
creates a system-effect APO by **COM aggregation**. Our class factory began, as
almost every hand-written one does, with:

```cpp
if (outer) return CLASS_E_NOAGGREGATION;
```

So the engine was told "no", and skipped the effect **in complete silence** --
no error, no event log entry, the CLSID absent from an ETW trace. It is
indistinguishable from Windows refusing third-party APOs, and it is one line of
our own code.

**If you write an APO, support aggregation.** The factory must accept a non-null
controlling unknown, require `IID_IUnknown` when aggregated, and hand back the
object's *non-delegating* IUnknown. The object needs the usual split: a
delegating IUnknown that forwards to the controlling unknown, and an inner
non-delegating one that owns the lifetime.

### Two more the engine wanted, in order

With aggregation fixed the handshake got further, and each refusal showed up as
a `QI: refused` line:

1. **IAudioSystemEffects3** `{C58B31CD-FC6A-4255-BC1F-AD29BB0A4A17}`, queried
   twice. This is the Windows 11 interface; Microsoft's docs say APOs "are
   expected to utilize" it. Implementing it (an empty controllable-effects list
   is a valid answer) moved the handshake on. Note `Initialize`'s `cbDataSize`
   changed from 88 to 80 once we claimed it -- the engine passes a different
   init struct.
2. **The free-threaded marshaler.** With `ThreadingModel=Both` the engine calls
   from any apartment; refusing `IMarshal` and `IAgileObject` sent it into a long
   fruitless round of marshalling probes. Aggregate the FTM with
   `CoCreateFreeThreadedMarshaler` and answer `IAgileObject`.

After all three, the sequence reaches:

```
ctor: created (aggregated=yes, ftm=yes)
Initialize: cbDataSize=80
IsInputFormatSupported: 2 ch, 32-bit, 48000 Hz -> 0x00000000
GetRegistrationProperties -> 0x00000000
```

### One more, and it explains the borrowed-CLSID dead end

It then stops. The reason is visible in that last line: the engine validates that
the CLSID an APO reports in `APO_REG_PROPERTIES.clsid` **matches the CLSID it
activated**. While borrowing Equalizer APO's CLSID for the test, ours reported
its own, and the mismatch is rejected right there -- silently again. A shipping
APO reports its own single CLSID and this never arises.

### Still open

Registering our *own* CLSID (or a brand-new one) into `,5` or `,7` still does not
get as far as `DllMain`, while Equalizer APO's CLSID in the same slot does. All
of registration shape, ACLs, path, value type, signing and a reboot have been
excluded. A full `reg query HKLM /f` for Equalizer APO's CLSID was started to
find any additional registration its installer performs that has not been
replicated; that search is the next thread to pull.

The aggregation, IAudioSystemEffects3, FTM and reg-properties findings are all
real and are now in `EightDApo` regardless -- without them the effect could never
have loaded even once that last question is answered.

## The shape of the remaining answer

The engine emits **no error of any kind** for our APO. Our CLSID does not appear
anywhere in the trace — not as a failure, not as an attempt. Windows is not
trying to load it and failing; it is not trying at all.

That points at the audio engine filtering the chain before instantiation, and
honouring only APOs it considers part of a properly installed audio driver
package, rather than CLSIDs injected into the property by a third party.

**The next experiments, in order:**

1. **Read Equalizer APO's installer source.** The brief says to use it as a
   reference and read how its installer does it, and that is now the highest
   value thing left — every cheap hypothesis has been tested and killed. Reading
   costs nothing and installs nothing. Tracing a live install is a fallback if
   the source is not clear enough.
2. Check whether the endpoint needs its effect *list* properties
   (`{d3993a3f-99c2-4402-b5ec-a92a0367664b},5/6/7`, which hold supported
   processing modes) extended, and whether the engine cross-checks the chain
   against them.
3. Test on an endpoint whose driver is the Microsoft generic one rather than
   Realtek's, in case the OEM package constrains what may be added.

## The constraint, restated because it is not negotiable

There is a standing temptation, when the APO route is blocked, to fall back on
WASAPI loopback through a virtual cable. **That is ruled out.** The product
requirement is that the user installs nothing but 8D Music, changes no output
device, and routes nothing by hand. A build that needs VB-Cable or VoiceMeeter
is not this product, however well it works.

That a development machine happens to have a virtual cable installed is not
evidence about users and must not be used as one.

So the APO is the path, and the job is to find what the audio engine wants.

## Tools left behind for this

* `windows/tools/play_to_endpoint.cpp` — opens a shared-mode stream on one named
  endpoint so an APO registered against it is actually instantiated, without
  changing the machine's default output.
* `PassthroughApo.cpp` — writes a line to
  `%ProgramData%\8DMusic\passthrough-loaded.txt` from `LockForProcess`. Note
  audiodg's restricted token may not be able to *create* that file, so
  pre-create it with a permissive ACL before relying on it. The authoritative
  check is the `audiodg.exe` module list, read from an elevated shell.
* ETW: `logman create trace ... -p {AE4BD3BE-F36F-45B6-8D21-BDD6FB832853}`, then
  `tracerpt` to XML and grep for `System_Effect_APO_Initialized`.
