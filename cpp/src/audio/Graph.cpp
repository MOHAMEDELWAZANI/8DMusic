#include "Graph.h"
#include <cstring>
#include <cstdlib>
#include <cstdio>

namespace eightd {

static std::string dictGet(const spa_dict* d, const char* key) {
    const char* v = d ? spa_dict_lookup(d, key) : nullptr;
    return v ? std::string(v) : std::string();
}

// The value arrives as JSON, e.g. {"name":"alsa_output..."}.  A full parser
// would be overkill for one string field.
static std::string jsonName(const std::string& raw) {
    const auto k = raw.find("\"name\"");
    if (k == std::string::npos) return {};
    auto c = raw.find(':', k);
    if (c == std::string::npos) return {};
    auto q1 = raw.find('"', c);
    if (q1 == std::string::npos) return {};
    auto q2 = raw.find('"', q1 + 1);
    if (q2 == std::string::npos) return {};
    return raw.substr(q1 + 1, q2 - q1 - 1);
}

static const pw_registry_events kRegistryEvents = {
    .version = PW_VERSION_REGISTRY_EVENTS,
    .global = Graph::onGlobal,
    .global_remove = Graph::onGlobalRemove,
};

static const pw_metadata_events kMetadataEvents = {
    .version = PW_VERSION_METADATA_EVENTS,
    .property = Graph::onMetadataProperty,
};

void Graph::attach(pw_core* core) {
    core_ = core;
    registry_ = pw_core_get_registry(core, PW_VERSION_REGISTRY, 0);
    pw_registry_add_listener(registry_, &registryHook_, &kRegistryEvents, this);
}

void Graph::detach() {
    if (metadata_) { spa_hook_remove(&metadataHook_); pw_proxy_destroy((pw_proxy*)metadata_); metadata_ = nullptr; }
    if (registry_) { spa_hook_remove(&registryHook_); pw_proxy_destroy((pw_proxy*)registry_); registry_ = nullptr; }
    core_ = nullptr;
    std::lock_guard<std::mutex> lock(mutex_);
    sinks_.clear(); streams_.clear(); defaultSink_.clear();
}

void Graph::onGlobal(void* data, uint32_t id, uint32_t, const char* type,
                     uint32_t, const spa_dict* props) {
    auto* g = static_cast<Graph*>(data);
    if (!props) return;

    if (!std::strcmp(type, PW_TYPE_INTERFACE_Metadata)) {
        if (dictGet(props, PW_KEY_METADATA_NAME) != "default" || g->metadata_) return;
        auto* m = (pw_metadata*)pw_registry_bind(g->registry_, id, type,
                                                 PW_VERSION_METADATA, 0);
        if (!m) return;
        g->metadata_ = m;
        g->metadataId_ = id;
        pw_metadata_add_listener(m, &g->metadataHook_, &kMetadataEvents, g);
        return;
    }
    if (std::strcmp(type, PW_TYPE_INTERFACE_Node)) return;

    const std::string cls = dictGet(props, PW_KEY_MEDIA_CLASS);
    const std::string name = dictGet(props, PW_KEY_NODE_NAME);
    const uint32_t serial = uint32_t(std::strtoul(
        dictGet(props, PW_KEY_OBJECT_SERIAL).c_str(), nullptr, 10));

    if (cls == "Audio/Sink") {
        SinkInfo s;
        s.id = id; s.serial = serial; s.name = name;
        s.description = dictGet(props, PW_KEY_NODE_DESCRIPTION);
        if (s.description.empty()) s.description = dictGet(props, PW_KEY_NODE_NICK);
        {
            std::lock_guard<std::mutex> lock(g->mutex_);
            g->sinks_[id] = std::move(s);
        }
        if (g->onSinksChanged) g->onSinksChanged();
    } else if (cls == "Stream/Output/Audio") {
        // Never track our own playback stream: routing it into the virtual
        // sink would feed our output straight back into our input.
        if (name.rfind(kNodePrefix, 0) == 0) return;
        StreamInfo s;
        s.id = id; s.serial = serial; s.name = name;
        s.app = dictGet(props, PW_KEY_APP_NAME);
        if (s.app.empty()) s.app = dictGet(props, PW_KEY_MEDIA_NAME);
        if (s.app.empty()) s.app = name;
        std::lock_guard<std::mutex> lock(g->mutex_);
        g->streams_[id] = std::move(s);
    }
}

void Graph::onGlobalRemove(void* data, uint32_t id) {
    auto* g = static_cast<Graph*>(data);
    bool sinkGone = false;
    {
        std::lock_guard<std::mutex> lock(g->mutex_);
        sinkGone = g->sinks_.erase(id) > 0;
        g->streams_.erase(id);
    }
    if (id == g->metadataId_ && g->metadata_) {
        spa_hook_remove(&g->metadataHook_);
        g->metadata_ = nullptr;
    }
    if (sinkGone && g->onSinksChanged) g->onSinksChanged();
}

int Graph::onMetadataProperty(void* data, uint32_t subject, const char* key,
                              const char*, const char* value) {
    auto* g = static_cast<Graph*>(data);
    if (subject != 0 || !key || std::strcmp(key, "default.audio.sink")) return 0;
    std::lock_guard<std::mutex> lock(g->mutex_);
    g->defaultSink_ = value ? jsonName(value) : std::string();
    return 0;
}

std::vector<SinkInfo> Graph::sinks() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<SinkInfo> out;
    out.reserve(sinks_.size());
    for (const auto& [id, s] : sinks_)
        if (s.name != kSinkName) out.push_back(s);      // never offer ourselves
    return out;
}

std::vector<StreamInfo> Graph::outputStreams() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<StreamInfo> out;
    out.reserve(streams_.size());
    for (const auto& [id, s] : streams_) out.push_back(s);
    return out;
}

uint32_t Graph::serialOf(const std::string& nodeName) const {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& [id, s] : sinks_) if (s.name == nodeName) return s.serial;
    return 0;
}

std::string Graph::defaultSinkName() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return defaultSink_;
}

bool Graph::hasMetadata() const { return metadata_ != nullptr; }

void Graph::setDefaultSink(const std::string& name) {
    if (!metadata_ || name.empty()) return;
    char json[512];
    std::snprintf(json, sizeof json, "{\"name\":\"%s\"}", name.c_str());
    pw_metadata_set_property(metadata_, 0, "default.configured.audio.sink",
                             "Spa:String:JSON", json);
    pw_metadata_set_property(metadata_, 0, "default.audio.sink",
                             "Spa:String:JSON", json);
}

void Graph::moveStream(uint32_t nodeId, uint32_t sinkSerial) {
    if (!metadata_) return;
    char value[32];
    std::snprintf(value, sizeof value, "%u", sinkSerial);
    pw_metadata_set_property(metadata_, nodeId, "target.object", "Spa:Id", value);
}

void Graph::clearStreamTarget(uint32_t nodeId) {
    if (!metadata_) return;
    pw_metadata_set_property(metadata_, nodeId, "target.object", nullptr, nullptr);
}

} // namespace eightd
