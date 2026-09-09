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

class EightDApo final
    : public IAudioProcessingObject
    , public IAudioProcessingObjectConfiguration
    , public IAudioProcessingObjectRT
    , public IAudioSystemEffects
{
public:
    EightDApo();
    virtual ~EightDApo();

    // IUnknown
    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override;
    STDMETHODIMP_(ULONG) AddRef() override;
    STDMETHODIMP_(ULONG) Release() override;

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
    std::atomic<ULONG> ref_{1};

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
