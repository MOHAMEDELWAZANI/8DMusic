// Settings persistence: a flat key=value file, written atomically.
#pragma once
#include "../dsp/Params.h"
#include "../audio/Engine.h"
#include <string>
#include <map>
#include <fstream>
#include <sstream>
#include <cstdlib>
#include <filesystem>

namespace eightd {

inline std::filesystem::path configPath() {
    const char* xdg = std::getenv("XDG_CONFIG_HOME");
    std::filesystem::path base = xdg && *xdg
        ? std::filesystem::path(xdg)
        : std::filesystem::path(std::getenv("HOME") ? std::getenv("HOME") : ".") / ".config";
    return base / "8dmusic-cpp" / "settings.conf";
}

inline std::map<std::string, std::string> readConfig() {
    std::map<std::string, std::string> kv;
    std::ifstream in(configPath());
    if (!in) return kv;
    std::string line;
    while (std::getline(in, line)) {
        const auto eq = line.find('=');
        if (eq == std::string::npos || line.empty() || line[0] == '#') continue;
        kv[line.substr(0, eq)] = line.substr(eq + 1);
    }
    return kv;
}

inline void writeConfig(const std::map<std::string, std::string>& kv) {
    const auto path = configPath();
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    const auto tmp = path.string() + ".tmp";
    {
        std::ofstream out(tmp, std::ios::trunc);
        if (!out) return;                  // settings are a convenience only
        for (const auto& [k, v] : kv) out << k << '=' << v << '\n';
    }
    std::filesystem::rename(tmp, path, ec);
}

} // namespace eightd
