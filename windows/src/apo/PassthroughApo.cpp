// A do-nothing APO, built as its own DLL.
//
// This exists to answer one question before any effect work is trusted:
// *can a DLL of ours get into the Windows audio path at all?*
//
// It copies input to output and does nothing else. No DSP, no shared memory,
// no settings, no threads. If audio still plays with this registered, the
// registration, the signing bypass, the COM class and the restart procedure
// are all correct, and anything that breaks afterwards is the effect's fault.
//
// WHY IT DERIVES FROM CBaseAudioProcessingObject
//
// The first version hand-rolled the COM object straight from the raw
// interfaces. It registered perfectly -- CoCreateInstance succeeded, the CLSID
// sat in the effect chain, the registration was byte-identical to Realtek's --
// and the audio engine silently never instantiated it. No error, no event log
// entry, the CLSID absent from an ETW trace entirely.
//
// The tell was in the imports. Every APO that *does* load on this machine --
// Realtek's, SmartAudio's, and Equalizer APO's too -- imports audioeng.dll.
// Ours imported only ole32/advapi32/kernel32. audioeng.dll is where Microsoft's
// CBaseAudioProcessingObject lives, and if the engine relies on anything that
// base class sets up, a hand-rolled object cannot be a drop-in for it.
//
// So this now does what every working example does: derive from the base class
// and let it handle registration properties, format validation and connection
// caching. Only APOProcess is genuinely ours.
//
// Build:   cmake --build build --config Release --target 8DMusicPassthrough
#include <windows.h>
#include <unknwn.h>
#include <audioenginebaseapo.h>
#include <baseaudioprocessingobject.h>
#include <audioengineextensionapo.h>
#include <mmreg.h>
#include <ks.h>
#include <ksmedia.h>
#include <atomic>
#include <new>
#include <cstring>
#include <cstdio>
#include <cstdarg>

// {1B7C4D22-9A03-4F51-B6E8-2C90A7F30E11}  -- deliberately NOT the effect's CLSID,
// so both can be registered and swapped without colliding.
// Defined, not merely declared: see the note in DllMain.cpp about <initguid.h>.
EXTERN_C const GUID CLSID_PassthroughApo =
    { 0x1b7c4d22, 0x9a03, 0x4f51, { 0xb6, 0xe8, 0x2c, 0x90, 0xa7, 0xf3, 0x0e, 0x11 } };

namespace {

std::atomic<LONG> g_objects{0};
HMODULE g_module = nullptr;

const wchar_t kName[]      = L"8D Music passthrough probe";
const wchar_t kCopyright[] = L"8D Music";

// The engine validates that the CLSID an APO reports here matches the CLSID it
// activated. A mismatch is rejected right after GetRegistrationProperties, with
// no error anywhere -- which is how borrowing another APO's CLSID for a test
// dead-ends. The probe therefore reports whatever CLSID it was created under.
//
// Not const: DllGetClassObject stamps the live CLSID into it. A shipping APO has
// exactly one CLSID and should just hardcode its own.
APO_REG_PROPERTIES g_regProperties = {
    /* clsid                   */ { 0x1b7c4d22, 0x9a03, 0x4f51, { 0xb6, 0xe8, 0x2c, 0x90, 0xa7, 0xf3, 0x0e, 0x11 } },
    /* Flags                   */ APO_FLAG_DEFAULT,
    /* szFriendlyName          */ L"8D Music passthrough probe",
    /* szCopyrightInfo         */ L"8D Music",
    /* u32MajorVersion         */ 1,
    /* u32MinorVersion         */ 0,
    /* u32MinInputConnections  */ 1,
    /* u32MaxInputConnections  */ 1,
    /* u32MinOutputConnections */ 1,
    /* u32MaxOutputConnections */ 1,
    /* u32MaxInstances         */ 0xFFFFFFFF,
    /* u32NumAPOInterfaces     */ 1,
    /* iidAPOInterfaceList     */ { __uuidof(IAudioProcessingObject) },
};


// Diagnostic log, appended from the engine's setup path only -- never from
// APOProcess. The probe exists to find out where the handshake fails, and the
// audio engine reports nothing when it rejects an APO, so the object has to say
// so itself.
void apolog(const char* fmt, ...) {
    wchar_t base[MAX_PATH]{};
    const UINT n = GetSystemWindowsDirectoryW(base, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return;
    wchar_t path[MAX_PATH]{};
    if (swprintf_s(path, L"%s\\Temp\\8dmusic-apo.log", base) < 0) return;

    HANDLE h = CreateFileW(path, FILE_APPEND_DATA,
                           FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                           OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return;

    char line[512]{};
    va_list ap;
    va_start(ap, fmt);
    const int len = _vsnprintf_s(line, _TRUNCATE, fmt, ap);
    va_end(ap);
    OutputDebugStringA(line);
    if (len > 0) {
        DWORD written = 0;
        WriteFile(h, line, static_cast<DWORD>(len), &written, nullptr);
        static const char kEol[2] = { '\r', '\n' };
        WriteFile(h, kEol, 2, &written, nullptr);
    }
    CloseHandle(h);
}

// Renders an IID as text so an unexpected QueryInterface shows up by name.
void iidText(REFIID riid, char* out, size_t n) {
    wchar_t w[64]{};
    StringFromGUID2(riid, w, 64);
    WideCharToMultiByte(CP_UTF8, 0, w, -1, out, static_cast<int>(n), nullptr, nullptr);
}

class PassthroughApo final
    : public CBaseAudioProcessingObject
    // IAudioSystemEffects2 derives from IAudioSystemEffects, so this covers
    // both. It is what marks the object as a *system* effect rather than a
    // bare APO.
    // IAudioSystemEffects3 derives from 2, which derives from 1, so this one
    // declaration covers all three. Windows 11 queries for 3 by IID and, on this
    // machine, walks away from an APO that does not answer.
    , public IAudioSystemEffects3
{
public:
    explicit PassthroughApo(IUnknown* outer)
        : CBaseAudioProcessingObject(&g_regProperties),
          inner_(this),
          outer_(outer ? outer : &inner_) {
        ++g_objects;
        // ThreadingModel=Both means the engine may use this object from any
        // apartment. Aggregating the free-threaded marshaler is how a COM object
        // says "no apartment affinity, call me directly"; without it COM has to
        // build a proxy, and the engine probes IMarshal/IAgileObject repeatedly
        // and gets nowhere.
        CoCreateFreeThreadedMarshaler(static_cast<IUnknown*>(&inner_), &ftm_);
        apolog("ctor: created (aggregated=%s, ftm=%s)",
               outer ? "yes" : "no", ftm_ ? "yes" : "no");
    }
    virtual ~PassthroughApo() {
        if (ftm_) { ftm_->Release(); ftm_ = nullptr; }
        --g_objects;
    }

    // The non-delegating IUnknown, handed back to whoever aggregates us.
    IUnknown* nonDelegating() { return &inner_; }

    // ---- The *delegating* IUnknown. Every interface the aggregate exposes
    // must forward to the controlling unknown, or reference counts and
    // QueryInterface identity break across the aggregate.
    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
        return outer_->QueryInterface(riid, ppv);
    }
    STDMETHODIMP_(ULONG) AddRef() override  { return outer_->AddRef(); }
    STDMETHODIMP_(ULONG) Release() override { return outer_->Release(); }

    // ---- IAudioSystemEffects2: no user-visible effects to report.
    STDMETHODIMP GetEffectsList(LPGUID* ppEffectsIds, UINT* pcEffects,
                                HANDLE /*Event*/) override {
        if (!ppEffectsIds || !pcEffects) return E_POINTER;
        *ppEffectsIds = nullptr;
        *pcEffects = 0;
        return S_OK;
    }

    // ---- IAudioSystemEffects3: the effect this probe exposes to the Windows
    // sound settings. It has none -- it is a passthrough -- so it reports an
    // empty list, which is a valid answer.
    STDMETHODIMP GetControllableSystemEffectsList(AUDIO_SYSTEMEFFECT** effects,
                                                  UINT* numEffects,
                                                  HANDLE /*event*/) override {
        if (!effects || !numEffects) return E_POINTER;
        *effects = nullptr;
        *numEffects = 0;
        apolog("GetControllableSystemEffectsList: reporting none");
        return S_OK;
    }

    STDMETHODIMP SetAudioSystemEffectState(GUID /*effectId*/,
                                           AUDIO_SYSTEMEFFECT_STATE /*state*/) override {
        return S_OK;
    }

    // ---- The engine hands system effects an APOInitSystemEffects blob. The
    // base class validates against a struct size we do not know here, so accept
    // whatever arrives: the probe's job is to load, not to be fussy.
    STDMETHODIMP Initialize(UINT32 cb, BYTE*) override {
        apolog("Initialize: cbDataSize=%u", cb);
        m_bIsInitialized = true;
        return S_OK;
    }

    STDMETHODIMP LockForProcess(UINT32 nIn, APO_CONNECTION_DESCRIPTOR** in,
                                UINT32 nOut, APO_CONNECTION_DESCRIPTOR** out) override {
        // Let the base validate and cache the connections -- that is the whole
        // point of using it.
        apolog("LockForProcess: nIn=%u nOut=%u", nIn, nOut);
        if (in && in[0] && in[0]->pFormat) {
            const WAVEFORMATEX* w = in[0]->pFormat->GetAudioFormat();
            if (w) apolog("  offered: %u ch, %u-bit, %lu Hz, tag %u, blockAlign %u",
                          w->nChannels, w->wBitsPerSample, w->nSamplesPerSec,
                          w->wFormatTag, w->nBlockAlign);
        }
        const HRESULT hr = CBaseAudioProcessingObject::LockForProcess(nIn, in, nOut, out);
        apolog("  base LockForProcess -> 0x%08lX", static_cast<unsigned long>(hr));
        if (FAILED(hr)) return hr;

        const WAVEFORMATEX* wf = in[0]->pFormat->GetAudioFormat();
        if (wf) {
            channels_ = wf->nChannels;
            bytesPerFrame_ = wf->nBlockAlign;
            reportLoaded(wf);
        }
        return S_OK;
    }

    STDMETHODIMP IsInputFormatSupported(IAudioMediaType* opposite,
                                        IAudioMediaType* requested,
                                        IAudioMediaType** supported) override {
        const HRESULT hr = CBaseAudioProcessingObject::IsInputFormatSupported(
                               opposite, requested, supported);
        if (requested) {
            const WAVEFORMATEX* w = requested->GetAudioFormat();
            if (w) apolog("IsInputFormatSupported: %u ch, %u-bit, %lu Hz -> 0x%08lX",
                          w->nChannels, w->wBitsPerSample, w->nSamplesPerSec,
                          static_cast<unsigned long>(hr));
        } else {
            apolog("IsInputFormatSupported: (null requested) -> 0x%08lX",
                   static_cast<unsigned long>(hr));
        }
        return hr;
    }

    STDMETHODIMP IsOutputFormatSupported(IAudioMediaType* opposite,
                                         IAudioMediaType* requested,
                                         IAudioMediaType** supported) override {
        const HRESULT hr = CBaseAudioProcessingObject::IsOutputFormatSupported(
                               opposite, requested, supported);
        apolog("IsOutputFormatSupported -> 0x%08lX", static_cast<unsigned long>(hr));
        return hr;
    }

    STDMETHODIMP GetLatency(HNSTIME* p) override {
        const HRESULT hr = CBaseAudioProcessingObject::GetLatency(p);
        apolog("GetLatency -> 0x%08lX", static_cast<unsigned long>(hr));
        return hr;
    }

    STDMETHODIMP GetRegistrationProperties(APO_REG_PROPERTIES** pp) override {
        const HRESULT hr = CBaseAudioProcessingObject::GetRegistrationProperties(pp);
        apolog("GetRegistrationProperties -> 0x%08lX", static_cast<unsigned long>(hr));
        return hr;
    }

    STDMETHODIMP GetInputChannelCount(UINT32* c) override {
        const HRESULT hr = CBaseAudioProcessingObject::GetInputChannelCount(c);
        apolog("GetInputChannelCount -> 0x%08lX (%u)",
               static_cast<unsigned long>(hr), c ? *c : 0);
        return hr;
    }

    STDMETHODIMP UnlockForProcess() override {
        apolog("UnlockForProcess");
        return CBaseAudioProcessingObject::UnlockForProcess();
    }

    STDMETHODIMP_(void) APOProcess(UINT32, APO_CONNECTION_PROPERTY** ppIn,
                                   UINT32, APO_CONNECTION_PROPERTY** ppOut) override {
        if (!ppIn || !ppOut) return;
        APO_CONNECTION_PROPERTY* in = ppIn[0];
        APO_CONNECTION_PROPERTY* out = ppOut[0];
        if (!in || !out) return;

        if (in->u32BufferFlags == BUFFER_VALID && in->pBuffer && out->pBuffer &&
            in->u32ValidFrameCount > 0) {
            if (in->pBuffer != out->pBuffer) {
                memcpy(reinterpret_cast<void*>(out->pBuffer),
                       reinterpret_cast<const void*>(in->pBuffer),
                       static_cast<size_t>(in->u32ValidFrameCount) * bytesPerFrame_);
            }
            out->u32ValidFrameCount = in->u32ValidFrameCount;
            out->u32BufferFlags = BUFFER_VALID;
        } else {
            out->u32ValidFrameCount = in->u32ValidFrameCount;
            out->u32BufferFlags = in->u32BufferFlags;
        }
    }

private:
    // Non-delegating IUnknown: the only thing that actually owns the object.
    class Inner final : public IUnknown {
    public:
        explicit Inner(PassthroughApo* owner) : owner_(owner) {}
        STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
            return owner_->innerQueryInterface(riid, ppv);
        }
        STDMETHODIMP_(ULONG) AddRef() override { return ++ref_; }
        STDMETHODIMP_(ULONG) Release() override {
            const ULONG n = --ref_;
            if (n == 0) delete owner_;   // Inner lives inside owner_
            return n;
        }
    private:
        PassthroughApo* owner_;
        std::atomic<ULONG> ref_{1};
    };

    HRESULT innerQueryInterface(REFIID riid, void** ppv) {
        if (!ppv) return E_POINTER;
        *ppv = nullptr;
        if (riid == __uuidof(IUnknown))
            *ppv = static_cast<IUnknown*>(&inner_);   // identity: the inner one
        else if (riid == __uuidof(IAudioProcessingObject))
            *ppv = static_cast<IAudioProcessingObject*>(this);
        else if (riid == __uuidof(IAudioProcessingObjectConfiguration))
            *ppv = static_cast<IAudioProcessingObjectConfiguration*>(this);
        else if (riid == __uuidof(IAudioProcessingObjectRT))
            *ppv = static_cast<IAudioProcessingObjectRT*>(this);
        else if (riid == __uuidof(IAudioSystemEffects))
            *ppv = static_cast<IAudioSystemEffects*>(
                       static_cast<IAudioSystemEffects3*>(this));
        else if (riid == __uuidof(IAudioSystemEffects2))
            *ppv = static_cast<IAudioSystemEffects2*>(
                       static_cast<IAudioSystemEffects3*>(this));
        else if (riid == __uuidof(IAudioSystemEffects3))
            *ppv = static_cast<IAudioSystemEffects3*>(this);
        // Agility marker: this object has no apartment affinity, so say so
        // rather than making COM marshal it.
        else if (riid == __uuidof(IAgileObject))
            *ppv = static_cast<IUnknown*>(&inner_);
        // Marshalling is delegated to the free-threaded marshaler.
        else if (riid == __uuidof(IMarshal) && ftm_)
            return ftm_->QueryInterface(riid, ppv);
        else {
            char t[80]{};
            iidText(riid, t, sizeof(t));
            apolog("QI: refused %s", t);
            return E_NOINTERFACE;
        }
        inner_.AddRef();
        return S_OK;
    }

    Inner      inner_;
    IUnknown*  outer_;
    IUnknown*  ftm_ = nullptr;

    // Proof of life. Runs in LockForProcess -- the stream setup path, once per
    // stream -- and never in APOProcess, so no file I/O on the realtime thread.
    // Best effort: if any of it fails, audio must still work.
    //
    // Logged to %SystemRoot%\Temp, not ProgramData: audiodg runs on a
    // restricted token and could not write to ProgramData at all, which made a
    // live APO look indistinguishable from one that never ran.
    // The authoritative check is audiodg's module list from an elevated shell.
    void reportLoaded(const WAVEFORMATEX* wf) {
        wchar_t base[MAX_PATH]{};
        const UINT n = GetSystemWindowsDirectoryW(base, MAX_PATH);
        if (n == 0 || n >= MAX_PATH) return;

        wchar_t path[MAX_PATH]{};
        if (swprintf_s(path, L"%s\\Temp\\8dmusic-apo.log", base) < 0) return;

        HANDLE h = CreateFileW(path, FILE_APPEND_DATA,
                               FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                               OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h == INVALID_HANDLE_VALUE) return;

        SYSTEMTIME t{};
        GetLocalTime(&t);
        char line[256]{};
        const int len = _snprintf_s(line, _TRUNCATE,
            "%04d-%02d-%02d %02d:%02d:%02d  loaded in pid %lu  "
            "%u ch, %u-bit, %lu Hz, blockAlign %u\r\n",
            t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond,
            GetCurrentProcessId(),
            wf->nChannels, wf->wBitsPerSample,
            wf->nSamplesPerSec, wf->nBlockAlign);
        if (len > 0) {
            DWORD written = 0;
            WriteFile(h, line, static_cast<DWORD>(len), &written, nullptr);
        }
        CloseHandle(h);
    }

    UINT32 channels_ = 2;
    UINT32 bytesPerFrame_ = 8;
};

class Factory final : public IClassFactory {
public:
    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        if (riid == __uuidof(IUnknown) || riid == __uuidof(IClassFactory)) {
            *ppv = static_cast<IClassFactory*>(this);
            AddRef();
            return S_OK;
        }
        // The engine takes our factory and then never calls CreateInstance, so
        // log what else it asks the *factory* for -- refusing something it needs
        // here would look exactly like that.
        char t[80]{};
        iidText(riid, t, sizeof(t));
        apolog("Factory QI: REFUSED %s", t);
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return ++ref_; }
    STDMETHODIMP_(ULONG) Release() override {
        const ULONG n = --ref_;
        if (n == 0) delete this;
        return n;
    }
    STDMETHODIMP CreateInstance(IUnknown* outer, REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        *ppv = nullptr;

        // The Windows audio engine AGGREGATES system-effect APOs: it calls this
        // with a non-null controlling unknown and asks for IID_IUnknown.
        // Returning CLASS_E_NOAGGREGATION here -- the reflexive thing to write --
        // makes the engine skip the APO in silence, with no error anywhere. That
        // single line was why nothing ever loaded.
        if (outer && riid != __uuidof(IUnknown)) return E_NOINTERFACE;

        auto* o = new (std::nothrow) PassthroughApo(outer);
        if (!o) return E_OUTOFMEMORY;
        const HRESULT hr = o->nonDelegating()->QueryInterface(riid, ppv);
        o->nonDelegating()->Release();
        return hr;
    }

    STDMETHODIMP LockServer(BOOL b) override { apolog("Factory LockServer(%d)", b); return S_OK; }
private:
    std::atomic<ULONG> ref_{1};
};

} // namespace

BOOL APIENTRY DllMain(HMODULE m, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_module = m;
        DisableThreadLibraryCalls(m);
        // DllMain definitely runs whenever the DLL is loaded. If this line
        // appears but the constructor's does not, the object is genuinely never
        // created; if neither appears, it is the logging that cannot write and
        // the object may be running fine. That distinction is the whole point.
        apolog("DllMain: DLL_PROCESS_ATTACH in pid %lu", GetCurrentProcessId());
    }
    return TRUE;
}

STDAPI DllGetClassObject(REFCLSID rclsid, REFIID riid, void** ppv) {
    if (!ppv) return E_POINTER;
    *ppv = nullptr;
    // Deliberately answers for ANY CLSID, not just its own.
    //
    // This is a diagnostic probe, and one of the things being diagnosed is
    // whether the audio engine will activate a *particular* CLSID at all --
    // it loads this DLL happily under Equalizer APO's CLSID but never under
    // the one registered for it. Accepting anything lets a fresh GUID be
    // tried without rebuilding, which is the experiment that distinguishes
    // "this CLSID is poisoned" from "our registration is wrong".
    //
    // A shipping APO must NOT do this; it must answer only for its own class.
    char t[80]{};
    iidText(rclsid, t, sizeof(t));
    apolog("DllGetClassObject: clsid=%s", t);
    g_regProperties.clsid = rclsid;   // report back exactly what we were asked for
    auto* f = new (std::nothrow) Factory();
    if (!f) return E_OUTOFMEMORY;
    const HRESULT hr = f->QueryInterface(riid, ppv);
    f->Release();
    return hr;
}

STDAPI DllCanUnloadNow() { return g_objects == 0 ? S_OK : S_FALSE; }

STDAPI DllRegisterServer() {
    wchar_t clsid[64]{};
    StringFromGUID2(CLSID_PassthroughApo, clsid, ARRAYSIZE(clsid));
    wchar_t path[MAX_PATH]{};
    if (!GetModuleFileNameW(g_module, path, ARRAYSIZE(path))) return E_FAIL;

    wchar_t key[192]{};
    HKEY h = nullptr;
    swprintf_s(key, L"SOFTWARE\\Classes\\CLSID\\%s\\InprocServer32", clsid);
    if (RegCreateKeyExW(HKEY_LOCAL_MACHINE, key, 0, nullptr, 0, KEY_WRITE,
                        nullptr, &h, nullptr) != ERROR_SUCCESS) return E_ACCESSDENIED;
    RegSetValueExW(h, nullptr, 0, REG_SZ, reinterpret_cast<const BYTE*>(path),
                   static_cast<DWORD>((wcslen(path) + 1) * sizeof(wchar_t)));
    RegSetValueExW(h, L"ThreadingModel", 0, REG_SZ,
                   reinterpret_cast<const BYTE*>(L"Both"), 5 * sizeof(wchar_t));
    RegCloseKey(h);
    return S_OK;
}

STDAPI DllUnregisterServer() {
    wchar_t clsid[64]{};
    StringFromGUID2(CLSID_PassthroughApo, clsid, ARRAYSIZE(clsid));
    wchar_t key[192]{};
    swprintf_s(key, L"SOFTWARE\\Classes\\CLSID\\%s\\InprocServer32", clsid);
    RegDeleteKeyW(HKEY_LOCAL_MACHINE, key);
    swprintf_s(key, L"SOFTWARE\\Classes\\CLSID\\%s", clsid);
    RegDeleteKeyW(HKEY_LOCAL_MACHINE, key);
    return S_OK;
}
