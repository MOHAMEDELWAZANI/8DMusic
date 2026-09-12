#include "EightDApo.h"
#include <ks.h>
#include <ksmedia.h>
#include <new>
#include <cstring>

namespace eightd {

// Defined in DllMain.cpp: keeps the DLL loaded while any APO instance lives.
void moduleAddRef();
void moduleRelease();

namespace {

// Windows shows this in the endpoint's effect list.
const wchar_t kCopyright[] = L"8D Music";
const wchar_t kName[]      = L"8D Music spatial engine";

bool isFloatStereo(const WAVEFORMATEX* wf, UINT32& rate, UINT32& channels) {
    if (!wf) return false;

    if (wf->wFormatTag == WAVE_FORMAT_EXTENSIBLE) {
        const auto* ext = reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(wf);
        if (ext->SubFormat != KSDATAFORMAT_SUBTYPE_IEEE_FLOAT) return false;
    } else if (wf->wFormatTag != WAVE_FORMAT_IEEE_FLOAT) {
        return false;
    }
    if (wf->wBitsPerSample != 32) return false;

    rate = wf->nSamplesPerSec;
    channels = wf->nChannels;
    // The effect is a two-eared model; anything else is passed through.
    return channels == 2;
}

} // namespace

// Hand the input to the output untouched.  Used for every unexpected state:
// the effect is allowed to stop working, but the audio is not.
//
// Realtime safe: a bounded memcpy and nothing else.
void EightDApo::passthrough(APO_CONNECTION_PROPERTY* in,
                            APO_CONNECTION_PROPERTY* out) {
    if (!out) return;

    if (!in || !in->pBuffer || !out->pBuffer || in->u32ValidFrameCount == 0) {
        // Nothing to copy.  Report silence rather than leaving the output
        // buffer holding whatever was in it last time.
        out->u32ValidFrameCount = 0;
        out->u32BufferFlags = BUFFER_SILENT;
        return;
    }

    if (in->pBuffer != out->pBuffer) {
        memcpy(reinterpret_cast<void*>(out->pBuffer),
               reinterpret_cast<const void*>(in->pBuffer),
               static_cast<size_t>(in->u32ValidFrameCount) * channels_ * sizeof(float));
    }
    out->u32ValidFrameCount = in->u32ValidFrameCount;
    out->u32BufferFlags = BUFFER_VALID;
}

EightDApo::EightDApo(IUnknown* outer)
    : inner_(this), outer_(outer ? outer : &inner_) {
    moduleAddRef();
    // ThreadingModel "Both": the engine may call from any apartment, so say we
    // have no apartment affinity rather than making COM build a proxy.
    CoCreateFreeThreadedMarshaler(static_cast<IUnknown*>(&inner_), &ftm_);
    // Read-only side of the block: the installer creates it. If it is not
    // there yet the effect runs with defaults rather than failing to load.
    shared_ = openSharedState(false);
}

EightDApo::~EightDApo() {
    if (ftm_) { ftm_->Release(); ftm_ = nullptr; }
    // Reference counted inside: audiodg holds one instance per endpoint, and
    // the first to be destroyed must not unmap the view the others still read.
    closeSharedState();
    moduleRelease();
}

// Delegating IUnknown: forwards to the controlling unknown when aggregated.
STDMETHODIMP EightDApo::QueryInterface(REFIID riid, void** ppv) {
    return outer_->QueryInterface(riid, ppv);
}
STDMETHODIMP_(ULONG) EightDApo::AddRef()  { return outer_->AddRef(); }
STDMETHODIMP_(ULONG) EightDApo::Release() { return outer_->Release(); }

// The real one. Reference counting belongs to the inner unknown.
HRESULT EightDApo::innerQueryInterface(REFIID riid, void** ppv) {
    if (!ppv) return E_POINTER;
    *ppv = nullptr;

    if (riid == __uuidof(IUnknown))
        *ppv = static_cast<IUnknown*>(&inner_);        // identity
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
    else if (riid == __uuidof(IAgileObject))
        *ppv = static_cast<IUnknown*>(&inner_);
    else if (riid == __uuidof(IMarshal) && ftm_)
        return ftm_->QueryInterface(riid, ppv);
    else
        return E_NOINTERFACE;

    inner_.AddRef();
    return S_OK;
}

// The effect is a single on/off thing driven from our own window, not from the
// Windows sound settings, so there is nothing to enumerate here. An empty list
// is a valid answer; refusing the call is not.
STDMETHODIMP EightDApo::GetEffectsList(LPGUID* ppEffectsIds, UINT* pcEffects,
                                       HANDLE /*Event*/) {
    if (!ppEffectsIds || !pcEffects) return E_POINTER;
    *ppEffectsIds = nullptr;
    *pcEffects = 0;
    return S_OK;
}

STDMETHODIMP EightDApo::GetControllableSystemEffectsList(
        AUDIO_SYSTEMEFFECT** effects, UINT* numEffects, HANDLE /*event*/) {
    if (!effects || !numEffects) return E_POINTER;
    *effects = nullptr;
    *numEffects = 0;
    return S_OK;
}

STDMETHODIMP EightDApo::SetAudioSystemEffectState(GUID /*effectId*/,
                                                  AUDIO_SYSTEMEFFECT_STATE /*state*/) {
    return S_OK;
}

STDMETHODIMP EightDApo::Reset() {
    processor_.reset();
    return S_OK;
}

// The orbit adds up to ~0.7 ms of interaural delay; the engine wants that
// declared so it can compensate elsewhere. HNSTIME is 100-nanosecond units.
STDMETHODIMP EightDApo::GetLatency(HNSTIME* pTime) {
    if (!pTime) return E_POINTER;
    *pTime = static_cast<HNSTIME>(kItdMaxS * 10'000'000.0);
    return S_OK;
}

STDMETHODIMP EightDApo::GetRegistrationProperties(APO_REG_PROPERTIES** ppRegProps) {
    if (!ppRegProps) return E_POINTER;

    auto* props = static_cast<APO_REG_PROPERTIES*>(
        CoTaskMemAlloc(sizeof(APO_REG_PROPERTIES)));
    if (!props) return E_OUTOFMEMORY;

    ZeroMemory(props, sizeof(APO_REG_PROPERTIES));
    props->clsid = CLSID_EightDApoMFX;
    props->Flags = APO_FLAG_DEFAULT;
    props->u32MinInputConnections  = 1;
    props->u32MaxInputConnections  = 1;
    props->u32MinOutputConnections = 1;
    props->u32MaxOutputConnections = 1;
    // Unlimited, as every APO that actually loads on this machine declares.
    // An SFX slot is instantiated per stream, so a cap of 1 would starve the
    // second application to start playing.
    props->u32MaxInstances         = 0xFFFFFFFF;
    props->u32NumAPOInterfaces     = 1;
    props->iidAPOInterfaceList[0]  = __uuidof(IAudioProcessingObject);
    wcscpy_s(props->szFriendlyName, ARRAYSIZE(props->szFriendlyName), kName);
    wcscpy_s(props->szCopyrightInfo, ARRAYSIZE(props->szCopyrightInfo), kCopyright);

    *ppRegProps = props;
    return S_OK;
}

STDMETHODIMP EightDApo::Initialize(UINT32, BYTE*) {
    // Nothing to configure from the endpoint blob; the real setup happens in
    // LockForProcess, where the negotiated format is finally known.
    return S_OK;
}

STDMETHODIMP EightDApo::IsInputFormatSupported(
    IAudioMediaType*, IAudioMediaType* pRequestedInputFormat,
    IAudioMediaType** ppSupportedInputFormat) {

    if (!pRequestedInputFormat) return E_POINTER;
    if (ppSupportedInputFormat) *ppSupportedInputFormat = nullptr;

    UINT32 rate = 0, channels = 0;
    if (!isFloatStereo(pRequestedInputFormat->GetAudioFormat(), rate, channels))
        return APOERR_FORMAT_NOT_SUPPORTED;

    if (ppSupportedInputFormat) {
        *ppSupportedInputFormat = pRequestedInputFormat;
        pRequestedInputFormat->AddRef();
    }
    return S_OK;
}

STDMETHODIMP EightDApo::IsOutputFormatSupported(
    IAudioMediaType* pInputFormat, IAudioMediaType* pRequestedOutputFormat,
    IAudioMediaType** ppSupportedOutputFormat) {
    // In and out are the same shape: frames in, frames out, no channel change.
    return IsInputFormatSupported(pInputFormat, pRequestedOutputFormat,
                                  ppSupportedOutputFormat);
}

STDMETHODIMP EightDApo::GetInputChannelCount(UINT32* pu32ChannelCount) {
    if (!pu32ChannelCount) return E_POINTER;
    *pu32ChannelCount = channels_;
    return S_OK;
}

STDMETHODIMP EightDApo::LockForProcess(
    UINT32 u32NumInputConnections, APO_CONNECTION_DESCRIPTOR** ppInputConnections,
    UINT32 u32NumOutputConnections, APO_CONNECTION_DESCRIPTOR** ppOutputConnections) {

    if (u32NumInputConnections != 1 || u32NumOutputConnections != 1)
        return APOERR_NUM_CONNECTIONS_INVALID;
    if (!ppInputConnections || !ppOutputConnections) return E_POINTER;

    const APO_CONNECTION_DESCRIPTOR* in = ppInputConnections[0];
    if (!in || !in->pFormat) return E_INVALIDARG;

    UINT32 rate = 0, channels = 0;
    if (!isFloatStereo(in->pFormat->GetAudioFormat(), rate, channels)) {
        // Refusing is the honest answer -- but refusing *silently* is not.
        // Record what we were offered so the window can say "this endpoint is
        // 5.1, the effect is stereo" instead of showing a bare WAITING.
        if (!shared_) shared_ = openSharedState(false);
        if (shared_) {
            const WAVEFORMATEX* wf = in->pFormat->GetAudioFormat();
            shared_->rejectedChannels = wf ? wf->nChannels : 0;
            shared_->rejectedBits     = wf ? wf->wBitsPerSample : 0;
            InterlockedExchange(&shared_->formatRejected, 1);
        }
        return APOERR_FORMAT_NOT_SUPPORTED;
    }

    rate_ = rate;
    channels_ = channels;
    maxFrames_ = in->u32MaxFrameCount;

    // Every buffer the processor will ever be handed is sized here, so
    // APOProcess can run without touching the heap.
    processor_.init(static_cast<float>(rate_), maxFrames_);

    if (!shared_) shared_ = openSharedState(false);
    if (shared_) {
        shared_->sampleRate = rate_;
        shared_->channels = channels_;
        InterlockedExchange(&shared_->formatRejected, 0);
        readParams(shared_, params_);
    }

    locked_ = true;
    return S_OK;
}

STDMETHODIMP EightDApo::UnlockForProcess() {
    locked_ = false;
    processor_.reset();
    return S_OK;
}

STDMETHODIMP_(UINT32) EightDApo::CalcInputFrames(UINT32 u32OutputFrameCount) {
    return u32OutputFrameCount;
}

STDMETHODIMP_(UINT32) EightDApo::CalcOutputFrames(UINT32 u32InputFrameCount) {
    return u32InputFrameCount;
}

STDMETHODIMP_(void) EightDApo::APOProcess(
    UINT32, APO_CONNECTION_PROPERTY** ppInputConnections,
    UINT32, APO_CONNECTION_PROPERTY** ppOutputConnections) {

    if (!ppInputConnections || !ppOutputConnections) return;

    APO_CONNECTION_PROPERTY* in = ppInputConnections[0];
    APO_CONNECTION_PROPERTY* out = ppOutputConnections[0];
    if (!in || !out) return;

    switch (in->u32BufferFlags) {
    case BUFFER_VALID: {
        const float* src = reinterpret_cast<const float*>(in->pBuffer);
        float* dst = reinterpret_cast<float*>(out->pBuffer);
        const UINT32 frames = in->u32ValidFrameCount;

        // Fail *open*, never to silence.  A user whose audio has gone quiet has
        // no way to connect that to us; a user who still hears unprocessed
        // audio has merely lost the effect.  The second failure is recoverable
        // and the first is not.
        if (!locked_ || !src || !dst || frames == 0) {
            passthrough(in, out);
            return;
        }

        // A settings change that arrives mid-write is skipped, not waited for.
        if (shared_) readParams(shared_, params_);

        processor_.process(src, dst, frames, params_);

        if (shared_) {
            writeTelemetry(shared_, processor_.angle(), processor_.distance(),
                           processor_.peakL(), processor_.peakR(), processor_.motion());
            // Proof of life for the GUI: an effect that is registered but never
            // called looks identical to one that is doing nothing.
            InterlockedExchange(&shared_->heartbeat, ++beat_);
        }

        out->u32ValidFrameCount = frames;
        out->u32BufferFlags = BUFFER_VALID;
        break;
    }

    case BUFFER_SILENT:
        // Still run the chain: reverb and delay tails have to decay rather than
        // stop dead when the source goes quiet.
        if (locked_ && out->pBuffer && in->u32ValidFrameCount > 0) {
            float* dst = reinterpret_cast<float*>(out->pBuffer);
            ZeroMemory(dst, static_cast<size_t>(in->u32ValidFrameCount) * channels_ * sizeof(float));
            if (shared_) readParams(shared_, params_);
            processor_.process(dst, dst, in->u32ValidFrameCount, params_);
            out->u32ValidFrameCount = in->u32ValidFrameCount;
            out->u32BufferFlags = BUFFER_VALID;
        } else {
            passthrough(in, out);
        }
        break;

    default:
        // An unrecognised flag is exactly the "unexpected state" the safety
        // rule is about: hand the audio on untouched.
        passthrough(in, out);
        break;
    }
}

} // namespace eightd
