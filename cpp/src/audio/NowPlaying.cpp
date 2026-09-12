#include "NowPlaying.h"
#include <systemd/sd-bus.h>
#include <chrono>
#include <regex>
#include <algorithm>
#include <cstring>
#include <cctype>

namespace eightd {

namespace {

constexpr const char* kBusPrefix  = "org.mpris.MediaPlayer2.";
constexpr const char* kObjectPath = "/org/mpris/MediaPlayer2";
constexpr const char* kPlayerIface= "org.mpris.MediaPlayer2.Player";
constexpr const char* kPropsIface = "org.freedesktop.DBus.Properties";
constexpr double kPollSeconds = 1.0;

double monotonic() {
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return char(std::tolower(c)); });
    return s;
}

std::string trim(const std::string& s) {
    const char* ws = " \t\n\r-–—";
    const auto a = s.find_first_not_of(ws);
    if (a == std::string::npos) return {};
    const auto b = s.find_last_not_of(ws);
    return s.substr(a, b - a + 1);
}

// Uploaders put a lot into a title that is not the name of the song.  These
// only ever remove or move text, so a title we fail to recognise is left alone.
const std::regex kChannelSuffix(R"(\s*-\s*Topic\s*$|\s*VEVO\s*$)",
                                std::regex::icase);
const std::regex kNoise(
    R"(\s*[\(\[]\s*(?:official\s+)?(?:music\s+)?(?:video|audio|lyrics?(?:\s+video)?|visuali[sz]er|hd|hq|4k|remastered(?:\s+[0-9]{4})?|explicit|clean|full\s+song)\s*[\)\]])",
    std::regex::icase);
const std::regex kFeatured(
    R"(\s*[\(\[]?\s*\b(?:feat|ft|featuring)\b\.?\s+([^)\]]+?)\s*[\)\]]?\s*$)",
    std::regex::icase);
// Deliberately conservative: "and" and "x" appear inside band names far too
// often to be treated as separators.
const std::regex kSplitArtists(R"(\s*(?:,|&|;)\s*)");

std::vector<std::string> splitArtists(const std::string& s) {
    std::vector<std::string> out;
    std::sregex_token_iterator it(s.begin(), s.end(), kSplitArtists, -1), end;
    for (; it != end; ++it) {
        const std::string part = trim(*it);
        if (!part.empty()) out.push_back(part);
    }
    return out;
}

} // namespace

// Streaming sites hand us the uploader as the artist and stuff everything else
// into the title, so "Eminem - Love The Way You Lie ft. Rihanna" with an artist
// of "EminemVEVO" has to come apart into the song and the two people on it.
void cleanTrack(std::string& title, std::vector<std::string>& artists) {
    std::vector<std::string> names;
    for (const auto& a : artists) {
        const std::string cleaned = trim(std::regex_replace(a, kChannelSuffix, ""));
        if (!cleaned.empty()) names.push_back(cleaned);
    }
    title = trim(title);

    // An uploader name is not a performer: if it was the only "artist" and the
    // title still carries the usual "artist - song", trust the title.
    const auto dash = title.find(" - ");
    if (dash != std::string::npos) {
        const std::string head = trim(title.substr(0, dash));
        const std::string tail = trim(title.substr(dash + 3));
        // The artist counts as an uploader when its name shows up right at the
        // front of the title.
        bool uploaderish = names.empty();
        if (!uploaderish) {
            const std::string head = lower(title).substr(
                0, std::min(names[0].size() + 2, title.size()));
            uploaderish = head.find(lower(names[0])) != std::string::npos;
        }
        if (!head.empty() && !tail.empty() && uploaderish) {
            if (names.empty()) names = splitArtists(head);
            title = tail;
        }
    }

    title = trim(std::regex_replace(title, kNoise, ""));

    std::smatch m;
    if (std::regex_search(title, m, kFeatured) && m.size() > 1) {
        const std::string featured = m[1].str();
        title = trim(title.substr(0, size_t(m.position(0))));
        for (auto& n : splitArtists(featured)) names.push_back(n);
    }

    std::vector<std::string> ordered;
    std::vector<std::string> seen;
    for (auto& raw : names) {
        const std::string name = trim(std::regex_replace(raw, kNoise, ""));
        if (name.empty()) continue;
        const std::string key = lower(name);
        if (std::find(seen.begin(), seen.end(), key) != seen.end()) continue;
        seen.push_back(key);
        ordered.push_back(name);
    }
    artists = std::move(ordered);
}

// --- Track ------------------------------------------------------------------

std::string Track::artistLine() const {
    std::string out;
    for (size_t i = 0; i < artists.size(); ++i) {
        if (i) out += " · ";
        out += artists[i];
    }
    return out;
}

std::string Track::initials() const {
    // Works in characters, not bytes: an Arabic or Cyrillic name has to give
    // its own first letter, not fall through to the placeholder.
    auto charAt = [](const std::string& s, size_t i) {
        size_t n = 1;
        const unsigned char c = static_cast<unsigned char>(s[i]);
        if ((c & 0xE0) == 0xC0) n = 2;
        else if ((c & 0xF0) == 0xE0) n = 3;
        else if ((c & 0xF8) == 0xF0) n = 4;
        return s.substr(i, std::min(n, s.size() - i));
    };
    auto isLetter = [](const std::string& ch) {
        if (ch.empty()) return false;
        const unsigned char c = static_cast<unsigned char>(ch[0]);
        if (c < 0x80) return std::isalnum(c) != 0;
        return true;                       // any non-ASCII character counts
    };
    auto firstAlnum = [&](const std::string& s) -> std::string {
        for (size_t i = 0; i < s.size(); ) {
            const std::string ch = charAt(s, i);
            if (isLetter(ch)) return ch;
            i += ch.size();
        }
        return {};
    };
    std::vector<std::string> names;
    for (const auto& a : artists) if (!firstAlnum(a).empty()) names.push_back(a);
    if (names.empty() && !firstAlnum(title).empty()) names.push_back(title);
    if (names.empty()) return "—";

    if (names.size() >= 2) {
        std::string a = firstAlnum(names[0]), b = firstAlnum(names[1]);
        if (!a.empty()) a[0] = char(std::toupper((unsigned char)a[0]));
        if (!b.empty()) b[0] = char(std::tolower((unsigned char)b[0]));
        return a + b;
    }
    std::string letters;
    int taken = 0;
    for (size_t i = 0; i < names[0].size() && taken < 2; ) {
        const std::string ch = charAt(names[0], i);
        if (isLetter(ch)) { letters += ch; ++taken; }
        i += ch.size();
    }
    if (!letters.empty() && static_cast<unsigned char>(letters[0]) < 0x80) {
        letters[0] = char(std::toupper((unsigned char)letters[0]));
        if (letters.size() > 1 && static_cast<unsigned char>(letters[1]) < 0x80)
            letters[1] = char(std::tolower((unsigned char)letters[1]));
    }
    return letters;
}

double Track::at(double now) const {
    const double elapsed = playing ? std::max(now - stamp, 0.0) : 0.0;
    const double pos = position + elapsed;
    return length > 0 ? std::min(pos, length) : pos;
}

// --- sd-bus helpers ----------------------------------------------------------

namespace {

// One MPRIS metadata value, flattened to what the panel actually needs.
struct Value {
    std::string str;
    std::vector<std::string> strv;
    int64_t i64 = 0;
    bool boolean = false;
    bool isNumber = false, isBool = false;
};

bool readVariant(sd_bus_message* m, Value& out) {
    const char* contents = nullptr;
    char type = 0;
    if (sd_bus_message_peek_type(m, &type, &contents) <= 0 || type != SD_BUS_TYPE_VARIANT)
        return false;
    if (sd_bus_message_enter_container(m, SD_BUS_TYPE_VARIANT, contents) <= 0)
        return false;

    bool ok = true;
    const std::string sig = contents ? contents : "";
    if (sig == "s" || sig == "o") {
        const char* s = nullptr;
        ok = sd_bus_message_read_basic(m, sig[0], &s) >= 0;
        if (ok && s) out.str = s;
    } else if (sig == "as") {
        if (sd_bus_message_enter_container(m, SD_BUS_TYPE_ARRAY, "s") > 0) {
            const char* s = nullptr;
            while (sd_bus_message_read_basic(m, SD_BUS_TYPE_STRING, &s) > 0)
                if (s) out.strv.emplace_back(s);
            sd_bus_message_exit_container(m);
        }
    } else if (sig == "x" || sig == "t" || sig == "i" || sig == "u" || sig == "n") {
        int64_t v = 0;
        if (sig == "x") ok = sd_bus_message_read_basic(m, 'x', &v) >= 0;
        else if (sig == "t") { uint64_t u = 0; ok = sd_bus_message_read_basic(m, 't', &u) >= 0; v = int64_t(u); }
        else if (sig == "i") { int32_t i = 0; ok = sd_bus_message_read_basic(m, 'i', &i) >= 0; v = i; }
        else if (sig == "u") { uint32_t u = 0; ok = sd_bus_message_read_basic(m, 'u', &u) >= 0; v = u; }
        else { int16_t i = 0; ok = sd_bus_message_read_basic(m, 'n', &i) >= 0; v = i; }
        out.i64 = v; out.isNumber = true;
    } else if (sig == "d") {
        double d = 0;
        ok = sd_bus_message_read_basic(m, 'd', &d) >= 0;
        out.i64 = int64_t(d); out.isNumber = true;
    } else if (sig == "b") {
        int b = 0;
        ok = sd_bus_message_read_basic(m, 'b', &b) >= 0;
        out.boolean = b != 0; out.isBool = true;
    } else {
        // Something we do not need (a nested dict, an image ref): skip it whole.
        sd_bus_message_skip(m, contents);
    }
    sd_bus_message_exit_container(m);
    return ok;
}

// Reads a{sv} into a callback, used for both the property set and Metadata.
template <typename Fn>
bool readDict(sd_bus_message* m, Fn&& fn) {
    if (sd_bus_message_enter_container(m, SD_BUS_TYPE_ARRAY, "{sv}") <= 0) return false;
    while (sd_bus_message_enter_container(m, SD_BUS_TYPE_DICT_ENTRY, "sv") > 0) {
        const char* key = nullptr;
        if (sd_bus_message_read_basic(m, SD_BUS_TYPE_STRING, &key) > 0 && key) {
            if (!fn(std::string(key), m)) sd_bus_message_skip(m, "v");
        }
        sd_bus_message_exit_container(m);
    }
    sd_bus_message_exit_container(m);
    return true;
}

std::string identityOf(sd_bus* bus, const std::string& name) {
    sd_bus_error err = SD_BUS_ERROR_NULL;
    sd_bus_message* reply = nullptr;
    std::string identity;
    if (sd_bus_call_method(bus, name.c_str(), kObjectPath, kPropsIface, "Get",
                           &err, &reply, "ss", "org.mpris.MediaPlayer2",
                           "Identity") >= 0 && reply) {
        Value v;
        if (readVariant(reply, v)) identity = v.str;
    }
    sd_bus_error_free(&err);
    if (reply) sd_bus_message_unref(reply);
    if (identity.empty() && name.size() > std::strlen(kBusPrefix)) {
        identity = name.substr(std::strlen(kBusPrefix));
        const auto dot = identity.find('.');
        if (dot != std::string::npos) identity = identity.substr(0, dot);
        if (!identity.empty())
            identity[0] = char(std::toupper((unsigned char)identity[0]));
    }
    return identity;
}

int pidOf(sd_bus* bus, const std::string& name) {
    sd_bus_error err = SD_BUS_ERROR_NULL;
    sd_bus_message* reply = nullptr;
    uint32_t pid = 0;
    if (sd_bus_call_method(bus, "org.freedesktop.DBus", "/org/freedesktop/DBus",
                           "org.freedesktop.DBus", "GetConnectionUnixProcessID",
                           &err, &reply, "s", name.c_str()) >= 0 && reply)
        sd_bus_message_read(reply, "u", &pid);
    sd_bus_error_free(&err);
    if (reply) sd_bus_message_unref(reply);
    return pid ? int(pid) : -1;
}

} // namespace

// --- the watcher --------------------------------------------------------------

NowPlaying::~NowPlaying() { stop(); }

void NowPlaying::start() {
    if (running_.exchange(true)) return;
    thread_ = std::thread([this] { loop(); });
}

void NowPlaying::stop() {
    if (!running_.exchange(false)) return;
    if (thread_.joinable()) thread_.join();
}

bool NowPlaying::has() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return has_;
}

Track NowPlaying::track() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return track_;
}

void NowPlaying::setCapturedApps(std::vector<std::string> apps) {
    std::lock_guard<std::mutex> lock(mutex_);
    capturedApps_ = std::move(apps);
}

void NowPlaying::previous()  { std::lock_guard<std::mutex> l(cmdMutex_); commands_.push_back({track().bus, "Previous"}); }
void NowPlaying::playPause() { std::lock_guard<std::mutex> l(cmdMutex_); commands_.push_back({track().bus, "PlayPause"}); }
void NowPlaying::next()      { std::lock_guard<std::mutex> l(cmdMutex_); commands_.push_back({track().bus, "Next"}); }

void NowPlaying::drainCommands(sd_bus* bus) {
    std::deque<std::pair<std::string, std::string>> pending;
    {
        std::lock_guard<std::mutex> lock(cmdMutex_);
        pending.swap(commands_);
    }
    for (const auto& [name, member] : pending) {
        if (name.empty()) continue;
        sd_bus_error err = SD_BUS_ERROR_NULL;
        sd_bus_call_method(bus, name.c_str(), kObjectPath, kPlayerIface,
                           member.c_str(), &err, nullptr, "");
        sd_bus_error_free(&err);
    }
}

void NowPlaying::loop() {
    sd_bus* bus = nullptr;
    if (sd_bus_open_user(&bus) < 0 || !bus) {
        running_.store(false);
        return;                       // no session bus: the panel simply stays empty
    }
    double nextPoll = 0;
    while (running_.load()) {
        drainCommands(bus);
        const double now = monotonic();
        if (now >= nextPoll) {
            nextPoll = now + kPollSeconds;
            poll(bus);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(60));
    }
    sd_bus_flush_close_unref(bus);
}

void NowPlaying::poll(sd_bus* bus) {
    // who is on the bus
    std::vector<std::string> names;
    {
        sd_bus_error err = SD_BUS_ERROR_NULL;
        sd_bus_message* reply = nullptr;
        if (sd_bus_call_method(bus, "org.freedesktop.DBus", "/org/freedesktop/DBus",
                               "org.freedesktop.DBus", "ListNames",
                               &err, &reply, "") >= 0 && reply) {
            if (sd_bus_message_enter_container(reply, SD_BUS_TYPE_ARRAY, "s") > 0) {
                const char* s = nullptr;
                while (sd_bus_message_read_basic(reply, SD_BUS_TYPE_STRING, &s) > 0)
                    if (s && std::strncmp(s, kBusPrefix, std::strlen(kBusPrefix)) == 0)
                        names.emplace_back(s);
                sd_bus_message_exit_container(reply);
            }
        }
        sd_bus_error_free(&err);
        if (reply) sd_bus_message_unref(reply);
    }

    std::vector<std::string> captured;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        captured = capturedApps_;
    }

    std::vector<Track> found;
    for (const auto& name : names) {
        sd_bus_error err = SD_BUS_ERROR_NULL;
        sd_bus_message* reply = nullptr;
        const int rc = sd_bus_call_method(bus, name.c_str(), kObjectPath, kPropsIface,
                                          "GetAll", &err, &reply, "s", kPlayerIface);
        if (rc < 0 || !reply) {          // gone, or refusing us
            sd_bus_error_free(&err);
            if (reply) sd_bus_message_unref(reply);
            continue;
        }

        Track t;
        t.bus = name;
        std::string status;
        std::vector<std::string> rawArtists;
        std::string rawTitle;

        readDict(reply, [&](const std::string& key, sd_bus_message* m) {
            if (key == "PlaybackStatus") {
                Value v; if (!readVariant(m, v)) return false;
                status = v.str;
            } else if (key == "Position") {
                Value v; if (!readVariant(m, v)) return false;
                t.position = double(v.i64) / 1e6;
            } else if (key == "CanGoPrevious") {
                Value v; if (!readVariant(m, v)) return false; t.canPrev = v.boolean;
            } else if (key == "CanGoNext") {
                Value v; if (!readVariant(m, v)) return false; t.canNext = v.boolean;
            } else if (key == "CanSeek") {
                Value v; if (!readVariant(m, v)) return false; t.canSeek = v.boolean;
            } else if (key == "Metadata") {
                // a variant holding a{sv}
                if (sd_bus_message_enter_container(m, SD_BUS_TYPE_VARIANT, "a{sv}") <= 0)
                    return false;
                readDict(m, [&](const std::string& mk, sd_bus_message* mm) {
                    if (mk == "xesam:title") {
                        Value v; if (!readVariant(mm, v)) return false; rawTitle = v.str;
                    } else if (mk == "xesam:artist") {
                        Value v; if (!readVariant(mm, v)) return false;
                        rawArtists = v.strv;
                        if (rawArtists.empty() && !v.str.empty()) rawArtists.push_back(v.str);
                    } else if (mk == "xesam:album") {
                        Value v; if (!readVariant(mm, v)) return false; t.album = v.str;
                    } else if (mk == "mpris:length") {
                        Value v; if (!readVariant(mm, v)) return false;
                        t.length = double(v.i64) / 1e6;
                    } else {
                        return false;
                    }
                    return true;
                });
                sd_bus_message_exit_container(m);
            } else {
                return false;
            }
            return true;
        });
        sd_bus_error_free(&err);
        sd_bus_message_unref(reply);

        if (status == "Stopped") continue;
        t.title = rawTitle;
        t.artists = rawArtists;
        cleanTrack(t.title, t.artists);
        if (!t.known()) continue;

        t.playing = (status == "Playing");
        t.stamp = monotonic();
        t.player = identityOf(bus, name);
        t.pid = pidOf(bus, name);
        const std::string who = lower(t.player);
        for (const auto& app : captured)
            if (!who.empty() && lower(app) == who) { t.captured = true; break; }
        found.push_back(std::move(t));
    }

    // Playing beats paused, ours beats somebody else's, detail beats none.
    const Track* best = nullptr;
    for (const auto& t : found) {
        if (!best) { best = &t; continue; }
        const auto rank = [](const Track& x) {
            return (x.playing ? 4 : 0) + (x.captured ? 2 : 0) + (x.known() ? 1 : 0);
        };
        if (rank(t) > rank(*best)) best = &t;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    if (best) { track_ = *best; has_ = true; }
    else      { has_ = false; }
}

} // namespace eightd
