// Plays an audio file through the 8D effect, so you can hear it.
//
// The APO route is not working yet (see docs/APO-RESEARCH.md Part 2), which
// leaves no way to actually listen to the Windows build. This closes that gap:
// it decodes a file with Media Foundation, runs it through the very same
// eightd::Processor the APO would use -- compiled in place from cpp/src/dsp,
// not copied -- and renders it with WASAPI.
//
// It is a listening tool, not the product. It proves the DSP works on Windows
// and lets you A/B the effect against the untouched source.
//
//   8dplay.exe song.mp3                 preset 1 (Classic 8D)
//   8dplay.exe song.mp3 -p 6            preset 6 (Extreme Spin)
//   8dplay.exe song.mp3 -dry            no effect, for comparison
//   8dplay.exe song.mp3 -p 3 -d {id}    play to a specific endpoint
//   8dplay.exe -list                    show presets and endpoints
//
// Press Ctrl+C to stop.
#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <functiondiscoverykeys_devpkey.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <cstdio>
#include <cmath>
#include <vector>
#include <string>

#include "Processor.h"
#include "Params.h"

#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "mfuuid.lib")

using namespace eightd;

namespace {

constexpr UINT32 kRate = 48000;
constexpr UINT32 kChannels = 2;

template <class T> void release(T*& p) { if (p) { p->Release(); p = nullptr; } }

void listPresets() {
    int n = 0;
    const Preset* ps = presets(n);
    wprintf(L"presets:\n");
    for (int i = 0; i < n; ++i) {
        wprintf(L"  %d  %hs\n", i + 1, ps[i].name);
    }
}

void listDevices() {
    IMMDeviceEnumerator* en = nullptr;
    if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                __uuidof(IMMDeviceEnumerator),
                                reinterpret_cast<void**>(&en)))) return;
    IMMDeviceCollection* col = nullptr;
    if (SUCCEEDED(en->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &col)) && col) {
        UINT n = 0;
        col->GetCount(&n);
        wprintf(L"\nactive output devices:\n");
        for (UINT i = 0; i < n; ++i) {
            IMMDevice* d = nullptr;
            if (FAILED(col->Item(i, &d)) || !d) continue;
            LPWSTR id = nullptr;
            d->GetId(&id);
            IPropertyStore* st = nullptr;
            if (SUCCEEDED(d->OpenPropertyStore(STGM_READ, &st)) && st) {
                PROPVARIANT v; PropVariantInit(&v);
                st->GetValue(PKEY_Device_FriendlyName, &v);
                wprintf(L"  %-52s %s\n",
                        (v.vt == VT_LPWSTR && v.pwszVal) ? v.pwszVal : L"(unnamed)",
                        id ? id : L"");
                PropVariantClear(&v);
                st->Release();
            }
            if (id) CoTaskMemFree(id);
            d->Release();
        }
        col->Release();
    }
    en->Release();
}

// Decodes the whole file to interleaved stereo float at 48 kHz.
//
// Decoding up front rather than streaming is deliberate: this tool exists to
// answer "does it sound right", and a decode stall mid-track would be
// indistinguishable from a glitch in the effect.
bool decode(const wchar_t* path, std::vector<float>& out) {
    IMFSourceReader* reader = nullptr;
    if (FAILED(MFCreateSourceReaderFromURL(path, nullptr, &reader)) || !reader) {
        fwprintf(stderr, L"could not open %s\n", path);
        return false;
    }

    IMFMediaType* want = nullptr;
    bool ok = false;
    if (SUCCEEDED(MFCreateMediaType(&want)) && want) {
        want->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
        want->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_Float);
        want->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, kChannels);
        want->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, kRate);
        want->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 32);
        want->SetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT, kChannels * 4);
        want->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, kRate * kChannels * 4);
        want->SetUINT32(MF_MT_ALL_SAMPLES_INDEPENDENT, TRUE);
        ok = SUCCEEDED(reader->SetCurrentMediaType(
                 MF_SOURCE_READER_FIRST_AUDIO_STREAM, nullptr, want));
    }
    release(want);
    if (!ok) {
        fwprintf(stderr, L"could not decode that file to 48 kHz stereo float\n");
        release(reader);
        return false;
    }
    reader->SetStreamSelection(MF_SOURCE_READER_FIRST_AUDIO_STREAM, TRUE);

    for (;;) {
        DWORD flags = 0;
        IMFSample* sample = nullptr;
        if (FAILED(reader->ReadSample(MF_SOURCE_READER_FIRST_AUDIO_STREAM, 0,
                                      nullptr, &flags, nullptr, &sample))) break;
        if (flags & MF_SOURCE_READERF_ENDOFSTREAM) { release(sample); break; }
        if (!sample) continue;

        IMFMediaBuffer* buf = nullptr;
        if (SUCCEEDED(sample->ConvertToContiguousBuffer(&buf)) && buf) {
            BYTE* p = nullptr; DWORD len = 0;
            if (SUCCEEDED(buf->Lock(&p, nullptr, &len)) && p) {
                const float* f = reinterpret_cast<const float*>(p);
                out.insert(out.end(), f, f + len / sizeof(float));
                buf->Unlock();
            }
            buf->Release();
        }
        release(sample);
    }
    release(reader);
    return !out.empty();
}

} // namespace

int wmain(int argc, wchar_t** argv) {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);

    if (argc < 2 || _wcsicmp(argv[1], L"-list") == 0) {
        wprintf(L"8dplay -- hear the 8D effect on Windows\n\n"
                L"  8dplay.exe <file> [-p N] [-dry] [-d {endpoint-id}]\n\n");
        listPresets();
        listDevices();
        CoUninitialize();
        return argc < 2 ? 1 : 0;
    }

    const wchar_t* file = argv[1];
    int presetIndex = 0;
    bool dry = false;
    const wchar_t* deviceId = nullptr;
    for (int i = 2; i < argc; ++i) {
        if (_wcsicmp(argv[i], L"-p") == 0 && i + 1 < argc) presetIndex = _wtoi(argv[++i]) - 1;
        else if (_wcsicmp(argv[i], L"-dry") == 0) dry = true;
        else if (_wcsicmp(argv[i], L"-d") == 0 && i + 1 < argc) deviceId = argv[++i];
    }

    int presetCount = 0;
    const Preset* ps = presets(presetCount);
    if (presetIndex < 0 || presetIndex >= presetCount) presetIndex = 0;

    Params p = ps[presetIndex].p;
    p.enabled = !dry;
    // The point is to hear the orbit, and a file played from silence would
    // otherwise start with the source parked.
    p.pauseWhenSilent = false;

    if (FAILED(MFStartup(MF_VERSION))) {
        fwprintf(stderr, L"Media Foundation would not start\n");
        CoUninitialize();
        return 1;
    }

    wprintf(L"decoding %s ...\n", file);
    std::vector<float> pcm;
    if (!decode(file, pcm)) { MFShutdown(); CoUninitialize(); return 1; }

    const size_t frames = pcm.size() / kChannels;
    wprintf(L"%.1f s, %hs\n", double(frames) / kRate,
            dry ? "DRY (no effect)" : ps[presetIndex].name);

    // ---- run it through the shared DSP
    std::vector<float> wet(pcm.size());
    Processor proc;
    const uint32_t block = 512;
    proc.init(float(kRate), block);
    for (size_t i = 0; i + block <= frames; i += block) {
        proc.process(pcm.data() + i * kChannels, wet.data() + i * kChannels, block, p);
    }

    // ---- render
    IMMDeviceEnumerator* en = nullptr;
    IMMDevice* dev = nullptr;
    CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                     __uuidof(IMMDeviceEnumerator), reinterpret_cast<void**>(&en));
    if (!en) { MFShutdown(); CoUninitialize(); return 1; }
    if (deviceId) en->GetDevice(deviceId, &dev);
    else          en->GetDefaultAudioEndpoint(eRender, eConsole, &dev);
    if (!dev) {
        fwprintf(stderr, L"no output device\n");
        release(en); MFShutdown(); CoUninitialize(); return 1;
    }

    IAudioClient* client = nullptr;
    dev->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                  reinterpret_cast<void**>(&client));
    WAVEFORMATEX* mix = nullptr;
    if (!client || FAILED(client->GetMixFormat(&mix)) || !mix) {
        fwprintf(stderr, L"could not open the output device\n");
        release(client); release(dev); release(en); MFShutdown(); CoUninitialize(); return 1;
    }

    if (FAILED(client->Initialize(AUDCLNT_SHAREMODE_SHARED, 0, 10'000'000, 0, mix, nullptr))) {
        fwprintf(stderr, L"could not initialise the output device\n");
        CoTaskMemFree(mix); release(client); release(dev); release(en);
        MFShutdown(); CoUninitialize(); return 1;
    }

    UINT32 bufFrames = 0;
    client->GetBufferSize(&bufFrames);
    IAudioRenderClient* render = nullptr;
    client->GetService(__uuidof(IAudioRenderClient), reinterpret_cast<void**>(&render));
    if (!render) {
        CoTaskMemFree(mix); release(client); release(dev); release(en);
        MFShutdown(); CoUninitialize(); return 1;
    }

    const UINT32 outCh = mix->nChannels;
    wprintf(L"playing through %u ch @ %lu Hz ... Ctrl+C to stop\n", outCh, mix->nSamplesPerSec);
    client->Start();

    size_t pos = 0;
    while (pos < frames) {
        UINT32 pad = 0;
        client->GetCurrentPadding(&pad);
        UINT32 avail = bufFrames - pad;
        if (avail == 0) { Sleep(10); continue; }
        if (avail > frames - pos) avail = static_cast<UINT32>(frames - pos);

        BYTE* raw = nullptr;
        if (FAILED(render->GetBuffer(avail, &raw)) || !raw) break;
        float* dst = reinterpret_cast<float*>(raw);
        for (UINT32 i = 0; i < avail; ++i) {
            const float L = wet[(pos + i) * kChannels];
            const float R = wet[(pos + i) * kChannels + 1];
            for (UINT32 c = 0; c < outCh; ++c) {
                dst[i * outCh + c] = (c == 0) ? L : (c == 1 ? R : 0.f);
            }
        }
        render->ReleaseBuffer(avail, 0);
        pos += avail;
        Sleep(5);
    }
    Sleep(400);
    client->Stop();
    wprintf(L"done\n");

    release(render);
    CoTaskMemFree(mix);
    release(client);
    release(dev);
    release(en);
    MFShutdown();
    CoUninitialize();
    return 0;
}
