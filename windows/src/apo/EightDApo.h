// The 8D effect as a Windows Audio Processing Object.
//
// Windows loads this DLL into audiodg.exe and hands it every buffer on its way
// to the endpoint, which is the same position the PipeWire sink occupies on
// Linux: after every application, before the hardware.  Nothing has to
// cooperate, and nothing has to be re-encoded.
//
// The rules inside APOProcess are the rules of any realtime callback: no
// allocation, no locks, no file or registry access.  eightd::Processor sizes
// everything in Initialize and allocates nothing afterwards, so the effect
// itself is already safe to run here.
#pragma once

#include <windows.h>
#include <unknwn.h>
#include <audioenginebaseapo.h>
#include <audioengineextensionapo.h>
#include <mmreg.h>
#include <atomic>

#include "../../../cpp/src/dsp/Processor.h"
#include "../shared/SharedState.h"

// {7F9E2C10-4E2B-4C77-9E3E-8D0C1B5A6D01}
DEFINE_GUID(CLSID_EightDApoMFX,
    0x7f9e2c10, 0x4e2b, 0x4c77, 0x9e, 0x3e, 0x8d, 0x0c, 0x1b, 0x5a, 0x6d, 0x01);

namespace eightd {

// WHY THIS IS AGGREGATED, AND WHY IT IMPLEMENTS IAudioSystemEffects3
//
// Both were found the hard way, by instrumenting a probe until the audio engine
// showed its hand.  The engine creates a system-effect APO by *aggregation*: it
// calls IClassFactory::CreateInstance with a non-null controlling unknown and
// asks for IID_IUnknown.  A factory that answers CLASS_E_NOAGGREGATION -- the
// reflexive thing to write -- is skipped in total silence: no error, no event
// log entry, the CLSID absent from an ETW trace.  It looks exactly like Windows
// refusing third-party APOs, and it is not.
//
// It then queries IAudioSystemEffects3 (Windows 11) and, with ThreadingModel
// "Both", expects the object to have no apartment affinity -- so the free-
// threaded marshaler is aggregated and IAgileObject is answered.
class EightDApo final
    : public IAudioProcessingObject
    , public IAudioProcessingObjectConfiguration
    , public IAudioProcessingObjectRT
    // 3 derives from 2 derives from 1, so this covers all three.
    , public IAudioSystemEffects3
{
public:
    explicit EightDApo(IUnknown* outer);
    virtual ~EightDApo();

    // The non-delegating IUnknown, handed to whoever aggregates us.
    IUnknown* nonDelegating() { return &inner_; }

    // IUnknown -- delegating: everything the aggregate exposes must forward to
    // the controlling unknown or refcounts and QI identity break.
    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override;
    STDMETHODIMP_(ULONG) AddRef() override;
    STDMETHODIMP_(ULONG) Release() override;

    // IAudioSystemEffects2 / 3
    STDMETHODIMP GetEffectsList(LPGUID* ppEffectsIds, UINT* pcEffects,
                                HANDLE Event) override;
    STDMETHODIMP GetControllableSystemEffectsList(AUDIO_SYSTEMEFFECT** effects,
                                                  UINT* numEffects,
                                                  HANDLE event) override;
    STDMETHODIMP SetAudioSystemEffectState(GUID effectId,
                                           AUDIO_SYSTEMEFFECT_STATE state) override;

    // IAudioProcessingObject
    STDMETHODIMP Reset() override;
    STDMETHODIMP GetLatency(HNSTIME* pTime) override;
    STDMETHODIMP GetRegistrationProperties(APO_REG_PROPERTIES** ppRegProps) override;
    STDMETHODIMP Initialize(UINT32 cbDataSize, BYTE* pbyData) override;
    STDMETHODIMP IsInputFormatSupported(IAudioMediaType* pOutputFormat,
                                        IAudioMediaType* pRequestedInputFormat,
                                        IAudioMediaType** ppSupportedInputFormat) override;
    STDMETHODIMP IsOutputFormatSupported(IAudioMediaType* pInputFormat,
                                         IAudioMediaType* pRequestedOutputFormat,
                                         IAudioMediaType** ppSupportedOutputFormat) override;
    STDMETHODIMP GetInputChannelCount(UINT32* pu32ChannelCount) override;

    // IAudioProcessingObjectConfiguration
    STDMETHODIMP LockForProcess(UINT32 u32NumInputConnections,
                                APO_CONNECTION_DESCRIPTOR** ppInputConnections,
                                UINT32 u32NumOutputConnections,
                                APO_CONNECTION_DESCRIPTOR** ppOutputConnections) override;
    STDMETHODIMP UnlockForProcess() override;

    // IAudioProcessingObjectRT
    STDMETHODIMP_(void) APOProcess(UINT32 u32NumInputConnections,
                                   APO_CONNECTION_PROPERTY** ppInputConnections,
                                   UINT32 u32NumOutputConnections,
                                   APO_CONNECTION_PROPERTY** ppOutputConnections) override;
    STDMETHODIMP_(UINT32) CalcInputFrames(UINT32 u32OutputFrameCount) override;
    STDMETHODIMP_(UINT32) CalcOutputFrames(UINT32 u32InputFrameCount) override;

private:
    void passthrough(APO_CONNECTION_PROPERTY* in, APO_CONNECTION_PROPERTY* out);

    // Non-delegating IUnknown: the only thing that actually owns the object.
    class Inner final : public IUnknown {
    public:
        explicit Inner(EightDApo* owner) : owner_(owner) {}
        STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
            return owner_->innerQueryInterface(riid, ppv);
        }
        STDMETHODIMP_(ULONG) AddRef() override { return ++ref_; }
        STDMETHODIMP_(ULONG) Release() override {
            const ULONG n = --ref_;
            if (n == 0) delete owner_;      // Inner lives inside owner_
            return n;
        }
    private:
        EightDApo* owner_;
        std::atomic<ULONG> ref_{1};
    };
    friend class Inner;

    HRESULT innerQueryInterface(REFIID riid, void** ppv);

    Inner      inner_;
    IUnknown*  outer_;
    IUnknown*  ftm_ = nullptr;

    Processor processor_;
    Params    params_{};             // last good snapshot
    SharedState* shared_ = nullptr;

    bool   locked_ = false;
    UINT32 channels_ = 2;
    UINT32 rate_ = 48000;
    UINT32 maxFrames_ = 0;
    LONG   beat_ = 0;
};

} // namespace eightd
