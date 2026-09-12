// Plays a quiet tone to one named endpoint, without changing the default device.
//
// Needed because an APO is only instantiated when a stream actually opens on
// the endpoint it is registered against. Switching the machine's default output
// would prove the same thing but changes the user's settings to do it; this
// opens a stream on a chosen endpoint directly and leaves everything else be.
//
//   play_to_endpoint.exe                       list render endpoints
//   play_to_endpoint.exe {id} [seconds] [gain] play to that endpoint
//
// Gain defaults to 0.02 -- audible if you are listening, unobtrusive if not.
#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <functiondiscoverykeys_devpkey.h>
#include <cstdio>
#include <cmath>
#include <string>

namespace {

const char* stateName(DWORD s) {
    switch (s) {
        case DEVICE_STATE_ACTIVE:     return "active";
        case DEVICE_STATE_DISABLED:   return "disabled";
        case DEVICE_STATE_NOTPRESENT: return "not present";
        case DEVICE_STATE_UNPLUGGED:  return "unplugged";
        default:                      return "?";
    }
}

int listDevices(IMMDeviceEnumerator* en) {
    IMMDeviceCollection* col = nullptr;
    if (FAILED(en->EnumAudioEndpoints(eRender, DEVICE_STATEMASK_ALL, &col))) return 1;

    std::wstring defaultId;
    IMMDevice* def = nullptr;
    if (SUCCEEDED(en->GetDefaultAudioEndpoint(eRender, eConsole, &def)) && def) {
        LPWSTR id = nullptr;
        if (SUCCEEDED(def->GetId(&id)) && id) { defaultId = id; CoTaskMemFree(id); }
        def->Release();
    }

    UINT n = 0;
    col->GetCount(&n);
    for (UINT i = 0; i < n; ++i) {
        IMMDevice* dev = nullptr;
        if (FAILED(col->Item(i, &dev)) || !dev) continue;

        LPWSTR id = nullptr;
        dev->GetId(&id);
        DWORD state = 0;
        dev->GetState(&state);

        IPropertyStore* store = nullptr;
        if (SUCCEEDED(dev->OpenPropertyStore(STGM_READ, &store)) && store) {
            PROPVARIANT v;
            PropVariantInit(&v);
            store->GetValue(PKEY_Device_FriendlyName, &v);
            wprintf(L"%s %-58s  %-12hs  %s\n",
                    (id && defaultId == id) ? L"*" : L" ",
                    (v.vt == VT_LPWSTR && v.pwszVal) ? v.pwszVal : L"(unnamed)",
                    stateName(state),
                    id ? id : L"");
            PropVariantClear(&v);
            store->Release();
        }
        if (id) CoTaskMemFree(id);
        dev->Release();
    }
    col->Release();
    wprintf(L"\n* = current default output (not modified by this tool)\n");
    return 0;
}

} // namespace

int wmain(int argc, wchar_t** argv) {
    if (FAILED(CoInitializeEx(nullptr, COINIT_MULTITHREADED))) return 1;

    IMMDeviceEnumerator* en = nullptr;
    if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                __uuidof(IMMDeviceEnumerator),
                                reinterpret_cast<void**>(&en)))) {
        fwprintf(stderr, L"no device enumerator\n");
        CoUninitialize();
        return 1;
    }

    if (argc < 2) {
        const int rc = listDevices(en);
        en->Release();
        CoUninitialize();
        return rc;
    }

    const double seconds = argc > 2 ? _wtof(argv[2]) : 5.0;
    const double gain    = argc > 3 ? _wtof(argv[3]) : 0.02;

    IMMDevice* dev = nullptr;
    if (FAILED(en->GetDevice(argv[1], &dev)) || !dev) {
        fwprintf(stderr, L"endpoint not found: %s\n", argv[1]);
        en->Release();
        CoUninitialize();
        return 1;
    }

    IAudioClient* client = nullptr;
    if (FAILED(dev->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                             reinterpret_cast<void**>(&client)))) {
        fwprintf(stderr, L"could not activate the endpoint (in use exclusively?)\n");
        dev->Release(); en->Release(); CoUninitialize();
        return 1;
    }

    // Shared mode on purpose: exclusive mode bypasses the audio engine, and
    // with it every APO -- which would defeat the entire point of this tool.
    WAVEFORMATEX* mix = nullptr;
    if (FAILED(client->GetMixFormat(&mix)) || !mix) {
        fwprintf(stderr, L"no mix format\n");
        client->Release(); dev->Release(); en->Release(); CoUninitialize();
        return 1;
    }
    wprintf(L"mix format: %u ch, %u-bit, %lu Hz, tag %u\n",
            mix->nChannels, mix->wBitsPerSample, mix->nSamplesPerSec, mix->wFormatTag);

    const REFERENCE_TIME dur = 10'000'000;   // 1 second of buffer
    HRESULT hr = client->Initialize(AUDCLNT_SHAREMODE_SHARED, 0, dur, 0, mix, nullptr);
    if (FAILED(hr)) {
        fwprintf(stderr, L"Initialize failed: 0x%08lX\n", hr);
        CoTaskMemFree(mix); client->Release(); dev->Release(); en->Release();
        CoUninitialize();
        return 1;
    }

    UINT32 frames = 0;
    client->GetBufferSize(&frames);

    IAudioRenderClient* render = nullptr;
    if (FAILED(client->GetService(__uuidof(IAudioRenderClient),
                                  reinterpret_cast<void**>(&render)))) {
        fwprintf(stderr, L"no render client\n");
        CoTaskMemFree(mix); client->Release(); dev->Release(); en->Release();
        CoUninitialize();
        return 1;
    }

    const bool isFloat =
        mix->wFormatTag == WAVE_FORMAT_IEEE_FLOAT ||
        (mix->wFormatTag == WAVE_FORMAT_EXTENSIBLE &&
         reinterpret_cast<WAVEFORMATEXTENSIBLE*>(mix)->SubFormat ==
             KSDATAFORMAT_SUBTYPE_IEEE_FLOAT);

    client->Start();
    wprintf(L"playing %.1f s at gain %.3f ...\n", seconds, gain);

    const double rate = mix->nSamplesPerSec;
    const UINT32 total = static_cast<UINT32>(seconds * rate);
    UINT32 done = 0;
    double phase = 0.0;
    const double step = 2.0 * 3.14159265358979 * 220.0 / rate;

    while (done < total) {
        UINT32 padding = 0;
        client->GetCurrentPadding(&padding);
        UINT32 avail = frames - padding;
        if (avail == 0) { Sleep(10); continue; }
        if (avail > total - done) avail = total - done;

        BYTE* buf = nullptr;
        if (FAILED(render->GetBuffer(avail, &buf)) || !buf) break;

        for (UINT32 i = 0; i < avail; ++i) {
            const double s = std::sin(phase) * gain;
            phase += step;
            for (UINT32 c = 0; c < mix->nChannels; ++c) {
                if (isFloat) {
                    reinterpret_cast<float*>(buf)[i * mix->nChannels + c] =
                        static_cast<float>(s);
                } else if (mix->wBitsPerSample == 16) {
                    reinterpret_cast<short*>(buf)[i * mix->nChannels + c] =
                        static_cast<short>(s * 32767.0);
                }
            }
        }
        render->ReleaseBuffer(avail, 0);
        done += avail;
        Sleep(5);
    }

    Sleep(300);
    client->Stop();
    wprintf(L"done\n");

    render->Release();
    CoTaskMemFree(mix);
    client->Release();
    dev->Release();
    en->Release();
    CoUninitialize();
    return 0;
}
