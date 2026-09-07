// A live view of the PipeWire graph.
//
// The Python build shelled out to pw-dump every couple of seconds and parsed a
// megabyte of JSON to answer "what sinks exist".  Here the registry pushes
// changes to us, so the same questions cost nothing and the answers are never
// stale.
#pragma once
#include <pipewire/pipewire.h>
#include <pipewire/extensions/metadata.h>
#include <string>
#include <vector>
#include <map>
#include <mutex>
#include <functional>

namespace eightd {

inline constexpr const char* kNodePrefix  = "eight_d_music";
inline constexpr const char* kSinkName    = "eight_d_music_sink";
inline constexpr const char* kSinkDesc    = "8D Music";
inline constexpr const char* kPlaybackName= "eight_d_music_output";

struct SinkInfo {
    uint32_t id = 0, serial = 0;
    std::string name, description;
    const std::string& label() const { return description.empty() ? name : description; }
};

struct StreamInfo {
    uint32_t id = 0, serial = 0;
    std::string name, app;
};

class Graph {
public:
    // Must be called with the thread loop locked.
    void attach(pw_core* core);
    void detach();

    std::vector<SinkInfo>   sinks() const;
    std::vector<StreamInfo> outputStreams() const;
    std::string defaultSinkName() const;
    uint32_t serialOf(const std::string& nodeName) const;
    bool hasMetadata() const;

    // All of these need the thread loop locked by the caller.
    void setDefaultSink(const std::string& name);
    void moveStream(uint32_t nodeId, uint32_t sinkSerial);
    void clearStreamTarget(uint32_t nodeId);

    // Called on the pipewire thread whenever the sink list changes.
    std::function<void()> onSinksChanged;

    // PipeWire event tables take plain function pointers, so these are public.
    static void onGlobal(void* data, uint32_t id, uint32_t perms, const char* type,
                         uint32_t version, const spa_dict* props);
    static void onGlobalRemove(void* data, uint32_t id);
    static int  onMetadataProperty(void* data, uint32_t subject, const char* key,
                                   const char* type, const char* value);

private:

    pw_core* core_ = nullptr;
    pw_registry* registry_ = nullptr;
    spa_hook registryHook_{};
    pw_metadata* metadata_ = nullptr;
    spa_hook metadataHook_{};
    uint32_t metadataId_ = 0;

    mutable std::mutex mutex_;
    std::map<uint32_t, SinkInfo> sinks_;
    std::map<uint32_t, StreamInfo> streams_;
    std::string defaultSink_;
};

} // namespace eightd
