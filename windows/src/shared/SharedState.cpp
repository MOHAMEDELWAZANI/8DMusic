#include "SharedState.h"
#include <sddl.h>

namespace eightd {

namespace {
HANDLE g_mapping = nullptr;
SharedState* g_view = nullptr;

// audiodg.exe runs at low integrity in a different session.  Without an
// explicit label it cannot open a mapping created by the desktop, and the APO
// then runs with default parameters forever -- which looks exactly like "the
// GUI does nothing".
//
//   D:(A;;GA;;;WD)   everyone, generic all
//   S:(ML;;NW;;;LW)  low integrity label, no write-up restriction
constexpr wchar_t kSddl[] = L"D:(A;;GA;;;WD)S:(ML;;NW;;;LW)";
}

SharedState* openSharedState(bool create) {
    if (g_view) return g_view;

    if (create) {
        SECURITY_ATTRIBUTES sa{};
        sa.nLength = sizeof(sa);
        sa.bInheritHandle = FALSE;
        if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(
                kSddl, SDDL_REVISION_1, &sa.lpSecurityDescriptor, nullptr)) {
            return nullptr;
        }
        g_mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, &sa, PAGE_READWRITE,
                                       0, sizeof(SharedState), kSharedName);
        LocalFree(sa.lpSecurityDescriptor);
    } else {
        g_mapping = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, kSharedName);
    }
    if (!g_mapping) return nullptr;

    const bool existed = (GetLastError() == ERROR_ALREADY_EXISTS);
    g_view = static_cast<SharedState*>(
        MapViewOfFile(g_mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(SharedState)));
    if (!g_view) {
        CloseHandle(g_mapping);
        g_mapping = nullptr;
        return nullptr;
    }

    if (create && !existed) {
        ZeroMemory(g_view, sizeof(SharedState));
        g_view->magic = kMagic;
        g_view->version = 1;
        g_view->params = Params{};
    }

    // A block left by an older build is not ours to interpret.
    if (g_view->magic != kMagic) {
        if (!create) { closeSharedState(); return nullptr; }
        ZeroMemory(g_view, sizeof(SharedState));
        g_view->magic = kMagic;
        g_view->version = 1;
        g_view->params = Params{};
    }
    return g_view;
}

void closeSharedState() {
    if (g_view) { UnmapViewOfFile(g_view); g_view = nullptr; }
    if (g_mapping) { CloseHandle(g_mapping); g_mapping = nullptr; }
}

} // namespace eightd
