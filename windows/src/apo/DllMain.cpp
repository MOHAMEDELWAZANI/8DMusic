// COM plumbing: what Windows needs in order to find and load the effect.
#include "EightDApo.h"

// DEFINE_GUID in the header only *declares* the symbol. Exactly one
// translation unit has to define it. Doing it by hand rather than via
// <initguid.h> is deliberate: initguid instantiates every DEFINE_GUID in every
// header pulled in after it, including the APO ones, which then collide with
// the copies already in uuid.lib.
EXTERN_C const GUID CLSID_EightDApoMFX =
    { 0x7f9e2c10, 0x4e2b, 0x4c77, { 0x9e, 0x3e, 0x8d, 0x0c, 0x1b, 0x5a, 0x6d, 0x01 } };
#include <new>
#include <cstdio>

namespace {
std::atomic<LONG> g_objects{0};
std::atomic<LONG> g_locks{0};
HMODULE g_module = nullptr;
} // namespace

// The live-object count has to be kept by the object itself, not by the class
// factory: CreateInstance is not the only thing that ends up holding one, and
// counting there leaked the count permanently, so DllCanUnloadNow could never
// return S_OK and the DLL could never be unloaded.
namespace eightd {
void moduleAddRef()  { ++g_objects; }
void moduleRelease() { --g_objects; }
}

namespace {

class ClassFactory final : public IClassFactory {
public:
    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        if (riid == __uuidof(IUnknown) || riid == __uuidof(IClassFactory)) {
            *ppv = static_cast<IClassFactory*>(this);
            AddRef();
            return S_OK;
        }
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

        // The audio engine AGGREGATES system-effect APOs -- non-null controlling
        // unknown, IID_IUnknown requested. Returning CLASS_E_NOAGGREGATION here
        // makes the engine skip the effect without reporting anything at all,
        // anywhere. That one line is why nothing ever loaded.
        if (outer && riid != __uuidof(IUnknown)) return E_NOINTERFACE;

        auto* apo = new (std::nothrow) eightd::EightDApo(outer);
        if (!apo) return E_OUTOFMEMORY;
        const HRESULT hr = apo->nonDelegating()->QueryInterface(riid, ppv);
        apo->nonDelegating()->Release();
        return hr;
    }

    STDMETHODIMP LockServer(BOOL lock) override {
        if (lock) ++g_locks; else --g_locks;
        return S_OK;
    }

private:
    std::atomic<ULONG> ref_{1};
};

// The APO is registered per audio endpoint by the installer; these two keys are
// what makes the class loadable in the first place.
LONG setKey(HKEY root, const wchar_t* path, const wchar_t* name, const wchar_t* value) {
    HKEY key = nullptr;
    LONG r = RegCreateKeyExW(root, path, 0, nullptr, REG_OPTION_NON_VOLATILE,
                             KEY_WRITE, nullptr, &key, nullptr);
    if (r != ERROR_SUCCESS) return r;
    r = RegSetValueExW(key, name, 0, REG_SZ,
                       reinterpret_cast<const BYTE*>(value),
                       static_cast<DWORD>((wcslen(value) + 1) * sizeof(wchar_t)));
    RegCloseKey(key);
    return r;
}

void clsidToString(const GUID& g, wchar_t* out, size_t n) {
    StringFromGUID2(g, out, static_cast<int>(n));
}

} // namespace

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_module = module;
        DisableThreadLibraryCalls(module);
    }
    return TRUE;
}

STDAPI DllGetClassObject(REFCLSID rclsid, REFIID riid, void** ppv) {
    if (!ppv) return E_POINTER;
    *ppv = nullptr;
    if (rclsid != CLSID_EightDApoMFX) return CLASS_E_CLASSNOTAVAILABLE;

    auto* factory = new (std::nothrow) ClassFactory();
    if (!factory) return E_OUTOFMEMORY;
    const HRESULT hr = factory->QueryInterface(riid, ppv);
    factory->Release();
    return hr;
}

STDAPI DllCanUnloadNow() {
    return (g_objects == 0 && g_locks == 0) ? S_OK : S_FALSE;
}

STDAPI DllRegisterServer() {
    wchar_t clsid[64]{};
    clsidToString(CLSID_EightDApoMFX, clsid, ARRAYSIZE(clsid));

    wchar_t path[MAX_PATH]{};
    if (!GetModuleFileNameW(g_module, path, ARRAYSIZE(path))) return E_FAIL;

    wchar_t key[160]{};
    swprintf_s(key, L"CLSID\\%s", clsid);
    if (setKey(HKEY_CLASSES_ROOT, key, nullptr, L"8D Music spatial engine") != ERROR_SUCCESS)
        return E_ACCESSDENIED;

    swprintf_s(key, L"CLSID\\%s\\InprocServer32", clsid);
    if (setKey(HKEY_CLASSES_ROOT, key, nullptr, path) != ERROR_SUCCESS)
        return E_ACCESSDENIED;
    // Both apartments: audiodg decides, not us.
    setKey(HKEY_CLASSES_ROOT, key, L"ThreadingModel", L"Both");

    return S_OK;
}

STDAPI DllUnregisterServer() {
    wchar_t clsid[64]{};
    clsidToString(CLSID_EightDApoMFX, clsid, ARRAYSIZE(clsid));

    wchar_t key[160]{};
    swprintf_s(key, L"CLSID\\%s\\InprocServer32", clsid);
    RegDeleteKeyW(HKEY_CLASSES_ROOT, key);
    swprintf_s(key, L"CLSID\\%s", clsid);
    RegDeleteKeyW(HKEY_CLASSES_ROOT, key);
    return S_OK;
}
