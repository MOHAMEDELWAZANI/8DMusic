#include "SharedState.h"
#include <sddl.h>
#include <shlobj.h>
#include <aclapi.h>
#include <cstdio>

namespace eightd {

namespace {

HANDLE       g_file    = INVALID_HANDLE_VALUE;
HANDLE       g_mapping = nullptr;
SharedState* g_view    = nullptr;
LONG         g_refs    = 0;
wchar_t      g_path[MAX_PATH]{};

// audiodg.exe runs in session 0 with a restricted token.  Without an explicit
// grant it cannot open a file the desktop created, and the APO then runs with
// default parameters forever -- which looks exactly like "the GUI does
// nothing".
//
//   D:(A;;GA;;;WD)   everyone, generic all
//     (A;;GA;;;AC)   all application packages
//     (A;;GA;;;S-1-15-2-2)  all restricted application packages
//   S:(ML;;NW;;;LW)  low integrity label, no write-up restriction
constexpr wchar_t kSddl[] =
    L"D:(A;;GA;;;WD)(A;;GA;;;AC)(A;;GA;;;S-1-15-2-2)S:(ML;;NW;;;LW)";

bool buildPath() {
    if (g_path[0]) return true;

    wchar_t base[MAX_PATH]{};
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_COMMON_APPDATA, nullptr,
                                SHGFP_TYPE_CURRENT, base))) {
        return false;
    }
    if (swprintf_s(g_path, L"%s\\8DMusic\\state.bin", base) < 0) {
        g_path[0] = 0;
        return false;
    }
    return true;
}

// Applies our DACL + integrity label to an existing file.  Separated out
// because the installer may have created the directory already.
void relabel(const wchar_t* path) {
    PSECURITY_DESCRIPTOR sd = nullptr;
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(
            kSddl, SDDL_REVISION_1, &sd, nullptr)) {
        return;
    }

    BOOL present = FALSE, defaulted = FALSE;
    PACL dacl = nullptr;
    if (GetSecurityDescriptorDacl(sd, &present, &dacl, &defaulted) && present) {
        SetNamedSecurityInfoW(const_cast<wchar_t*>(path), SE_FILE_OBJECT,
                              DACL_SECURITY_INFORMATION, nullptr, nullptr,
                              dacl, nullptr);
    }

    PACL sacl = nullptr;
    if (GetSecurityDescriptorSacl(sd, &present, &sacl, &defaulted) && present) {
        SetNamedSecurityInfoW(const_cast<wchar_t*>(path), SE_FILE_OBJECT,
                              LABEL_SECURITY_INFORMATION, nullptr, nullptr,
                              nullptr, sacl);
    }

    LocalFree(sd);
}

} // namespace

const wchar_t* sharedStatePath() {
    return buildPath() ? g_path : L"";
}

SharedState* openSharedState(bool create) {
    if (g_view) { ++g_refs; return g_view; }
    if (!buildPath()) return nullptr;

    if (create) {
        wchar_t dir[MAX_PATH]{};
        wcscpy_s(dir, g_path);
        wchar_t* slash = wcsrchr(dir, L'\\');
        if (slash) {
            *slash = 0;
            CreateDirectoryW(dir, nullptr);
        }
    }

    // Both sides open read/write and share fully -- the APO and the GUI hold
    // the file open at the same time by design.
    g_file = CreateFileW(g_path, GENERIC_READ | GENERIC_WRITE,
                         FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                         nullptr,
                         create ? OPEN_ALWAYS : OPEN_EXISTING,
                         FILE_ATTRIBUTE_NORMAL, nullptr);
    if (g_file == INVALID_HANDLE_VALUE) return nullptr;

    const bool fresh = create && (GetLastError() != ERROR_ALREADY_EXISTS);
    if (fresh) relabel(g_path);

    // The file must be at least the size of the view before it can be mapped.
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(g_file, &size) || size.QuadPart < LONGLONG(sizeof(SharedState))) {
        if (!create) { closeSharedState(); return nullptr; }
        LARGE_INTEGER want{};
        want.QuadPart = sizeof(SharedState);
        if (!SetFilePointerEx(g_file, want, nullptr, FILE_BEGIN) ||
            !SetEndOfFile(g_file)) {
            closeSharedState();
            return nullptr;
        }
    }

    g_mapping = CreateFileMappingW(g_file, nullptr, PAGE_READWRITE, 0,
                                   sizeof(SharedState), nullptr);
    if (!g_mapping) { closeSharedState(); return nullptr; }

    g_view = static_cast<SharedState*>(
        MapViewOfFile(g_mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(SharedState)));
    if (!g_view) { closeSharedState(); return nullptr; }

    // The audio thread reads this view every buffer.  Pinning it here, off the
    // realtime path, means it can never take a page fault in APOProcess.
    VirtualLock(g_view, sizeof(SharedState));

    // A block from an older build, or a fresh zero-filled file, is not ours to
    // interpret -- initialise it from the defaults instead.
    if (g_view->magic != kMagic || g_view->version != kVersion) {
        if (!create) { closeSharedState(); return nullptr; }
        ZeroMemory(g_view, sizeof(SharedState));
        g_view->magic = kMagic;
        g_view->version = kVersion;
        g_view->params = Params{};
    }

    g_refs = 1;
    return g_view;
}

void closeSharedState() {
    if (g_refs > 1) { --g_refs; return; }
    g_refs = 0;

    if (g_view) {
        VirtualUnlock(g_view, sizeof(SharedState));
        FlushViewOfFile(g_view, sizeof(SharedState));
        UnmapViewOfFile(g_view);
        g_view = nullptr;
    }
    if (g_mapping) { CloseHandle(g_mapping); g_mapping = nullptr; }
    if (g_file != INVALID_HANDLE_VALUE) { CloseHandle(g_file); g_file = INVALID_HANDLE_VALUE; }
}

} // namespace eightd
