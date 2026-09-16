// The bundled typeface.
//
// The interface is drawn in Figtree, which no distribution ships by default, so
// the font travels with the app and is handed to fontconfig for this process
// only -- nothing is installed and nothing outside the app sees it.  If the
// file is missing the interface still renders, in whichever family fontconfig
// picks from the list in `Ui::sans`.
#pragma once
#include <fontconfig/fontconfig.h>
#include <filesystem>
#include <string>
#include <unistd.h>
#include <climits>

namespace eightd {

inline std::filesystem::path exeDir() {
    char buf[PATH_MAX];
    const ssize_t n = ::readlink("/proc/self/exe", buf, sizeof buf - 1);
    if (n <= 0) return std::filesystem::current_path();
    buf[n] = '\0';
    return std::filesystem::path(buf).parent_path();
}

// Where the app's own files live: beside the binary when running from the
// source tree, under share/ when it has been installed.
inline std::filesystem::path assetDir() {
    const std::filesystem::path here = exeDir();
    for (const std::filesystem::path& p : {
             here / "assets",
             here.parent_path() / "share" / "8dmusic",
             std::filesystem::path("/usr/share/8dmusic"),
             std::filesystem::path("/usr/local/share/8dmusic")}) {
        std::error_code ec;
        if (std::filesystem::is_directory(p, ec)) return p;
    }
    return here / "assets";
}

inline void registerBundledFonts() {
    const auto dir = assetDir() / "fonts";
    std::error_code ec;
    if (!std::filesystem::is_directory(dir, ec)) return;
    for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
        if (!entry.is_regular_file()) continue;
        const auto ext = entry.path().extension().string();
        if (ext != ".ttf" && ext != ".otf") continue;
        FcConfigAppFontAddFile(
            FcConfigGetCurrent(),
            reinterpret_cast<const FcChar8*>(entry.path().c_str()));
    }
}

} // namespace eightd
