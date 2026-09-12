#include "NowPlaying.h"

#include <winrt/base.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Media.Control.h>

#include <algorithm>
#include <cwctype>
#include <chrono>

namespace eightd {

namespace {

using namespace winrt::Windows::Media::Control;

double monotonic() {
    return std::chrono::duration<double>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

// SourceAppUserModelId is an identifier, not a name: "Brave.exe", or for a
// packaged app something like "Microsoft.ZuneMusic_8wekyb3d8bbwe!App". Trim it
// to the part a person would recognise rather than pretending to a friendly
// name we have not actually looked up.
std::wstring tidyAppId(std::wstring id) {
    const size_t bang = id.rfind(L'!');
    if (bang != std::wstring::npos) id = id.substr(0, bang);
    const size_t under = id.find(L'_');
    if (under != std::wstring::npos) id = id.substr(0, under);
    const size_t slash = id.find_last_of(L"\\/");
    if (slash != std::wstring::npos) id = id.substr(slash + 1);
    if (id.size() > 4) {
        const std::wstring tail = id.substr(id.size() - 4);
        std::wstring lower;
        for (wchar_t c : tail) lower += wchar_t(towlower(c));
        if (lower == L".exe") id = id.substr(0, id.size() - 4);
    }
    // Chromium browsers use a hash-suffixed id -- Brave's is
    // "Brave.IPAWSHUTEQ5N3T232IPRANBZ2I". If everything after the first dot
    // looks like a hash (long, no lower case), it is machine noise, not a name.
    const size_t dot = id.find(L'.');
    if (dot != std::wstring::npos && dot > 0) {
        const std::wstring tail = id.substr(dot + 1);
        bool hashy = tail.size() >= 8;
        for (wchar_t c : tail) if (iswlower(c)) { hashy = false; break; }
        if (hashy) id = id.substr(0, dot);
    }
    if (!id.empty()) id[0] = wchar_t(towupper(id[0]));
    return id;
}

// Artists arrive as one string; players are inconsistent about the separator.
std::vector<std::wstring> splitArtists(const std::wstring& s) {
    std::vector<std::wstring> out;
    const std::wstring seps[] = { L" · ", L"; ", L", ", L" / ", L" & " };
    size_t start = 0;
    while (start <= s.size()) {
        size_t best = std::wstring::npos, bestLen = 0;
        for (const auto& sep : seps) {
            const size_t at = s.find(sep, start);
            if (at != std::wstring::npos && at < best) { best = at; bestLen = sep.size(); }
        }
        if (best == std::wstring::npos) {
            if (start < s.size()) out.push_back(s.substr(start));
            break;
        }
        if (best > start) out.push_back(s.substr(start, best - start));
        start = best + bestLen;
    }
    if (out.empty() && !s.empty()) out.push_back(s);
    return out;
}

} // namespace

std::wstring Track::artistLine() const {
    std::wstring out;
    for (size_t i = 0; i < artists.size(); ++i) {
        if (i) out += L" · ";
        out += artists[i];
    }
    return out;
}

std::wstring Track::initials() const {
    auto isLetter = [](wchar_t c) { return iswalnum(c) != 0; };
    auto firstAlnum = [&](const std::wstring& s) -> wchar_t {
        for (wchar_t c : s) if (isLetter(c)) return c;
        return 0;
    };

    std::vector<std::wstring> names;
    for (const auto& a : artists) if (firstAlnum(a)) names.push_back(a);
    if (names.empty() && firstAlnum(title)) names.push_back(title);
    if (names.empty()) return L"—";

    if (names.size() >= 2) {
        std::wstring out;
        out += wchar_t(towupper(firstAlnum(names[0])));
        out += wchar_t(towlower(firstAlnum(names[1])));
        return out;
    }
    std::wstring letters;
    for (wchar_t c : names[0]) {
        if (!isLetter(c)) continue;
        letters += letters.empty() ? wchar_t(towupper(c)) : wchar_t(towlower(c));
        if (letters.size() == 2) break;
    }
    return letters;
}

double Track::at(double now) const {
    const double elapsed = playing ? std::max(now - stamp, 0.0) : 0.0;
    const double pos = position + elapsed;
    return length > 0 ? std::min(pos, length) : pos;
}

NowPlaying::~NowPlaying() { stop(); }

void NowPlaying::start() {
    if (thread_.joinable()) return;
    quit_ = false;
    thread_ = std::thread([this] { run(); });
}

void NowPlaying::stop() {
    quit_ = true;
    if (thread_.joinable()) thread_.join();
}

Track NowPlaying::track() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return track_;
}

void NowPlaying::run() {
    // Multi-threaded apartment: this thread does nothing but block on WinRT
    // calls, and must not need a message pump.
    winrt::init_apartment(winrt::apartment_type::multi_threaded);

    GlobalSystemMediaTransportControlsSessionManager manager{ nullptr };
    try {
        manager = GlobalSystemMediaTransportControlsSessionManager::RequestAsync().get();
    } catch (...) {
        // No media platform, or it refused. Nothing to show, ever; leaving
        // has_ false makes the panel read "Nothing playing".
        return;
    }

    while (!quit_.load(std::memory_order_relaxed)) {
        Track t;
        bool found = false;
        try {
            auto session = manager.GetCurrentSession();
            if (session) {
                const auto props = session.TryGetMediaPropertiesAsync().get();
                t.title = props.Title().c_str();
                t.album = props.AlbumTitle().c_str();
                const std::wstring artist = props.Artist().c_str();
                if (!artist.empty()) t.artists = splitArtists(artist);

                const auto info = session.GetPlaybackInfo();
                t.playing = info.PlaybackStatus() ==
                    GlobalSystemMediaTransportControlsSessionPlaybackStatus::Playing;

                const auto tl = session.GetTimelineProperties();
                // EndTime is zero for live streams and for players that report
                // a position but never a duration; length 0 means "unknown"
                // and the bar draws empty rather than wrong.
                const double endS   = std::chrono::duration<double>(tl.EndTime()).count();
                const double startS = std::chrono::duration<double>(tl.StartTime()).count();
                const double posS   = std::chrono::duration<double>(tl.Position()).count();
                t.length   = std::max(0.0, endS - startS);
                t.position = std::max(0.0, posS - startS);
                t.stamp    = monotonic();

                t.player = tidyAppId(std::wstring(session.SourceAppUserModelId().c_str()));
                found = t.known();
            }
        } catch (...) {
            found = false;      // a session can vanish mid-call
        }

        {
            std::lock_guard<std::mutex> lock(mutex_);
            track_ = t;
        }
        has_.store(found, std::memory_order_relaxed);

        // Half a second: fast enough that a track change feels immediate, slow
        // enough that the polling costs nothing.
        for (int i = 0; i < 10 && !quit_.load(std::memory_order_relaxed); ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
}

} // namespace eightd
