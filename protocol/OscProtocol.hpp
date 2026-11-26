#pragma once

#include "protocol/OscParticleEmit.hpp"
#include "protocol/OscRoomPerson.hpp"
#include "protocol/OscTypes.hpp"
#include "protocol/OscUdpReceiver.hpp"
#include "scene/SceneRuntime.hpp"

#include <array>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace genesis {

class OscProtocol {
public:
    struct Settings {
        std::string bind_ip = "0.0.0.0";
        std::uint16_t port = 9000;
        bool listening = false;
        bool learn_enabled = true;
    };

    void setSettings(const Settings& settings);
    const Settings& settings() const { return settings_; }

    OscParticleEmitSettings& particleEmitSettings() { return particle_emit_; }
    const OscParticleEmitSettings& particleEmitSettings() const { return particle_emit_; }
    void setParticleEmitSettings(const OscParticleEmitSettings& settings) { particle_emit_ = settings; }

    OscRoomPersonSettings& roomPersonSettings() { return room_person_; }
    const OscRoomPersonSettings& roomPersonSettings() const { return room_person_; }
    void setRoomPersonSettings(const OscRoomPersonSettings& settings) { room_person_ = settings; }

    bool startListening(std::string* out_error = nullptr);
    void stopListening();
    bool isListening() const { return receiver_.isListening(); }

    void poll(double now_seconds);
    // Offline / test ingest of an already-decoded OSC message.
    void ingestDecodedMessage(const OscMessage& message, double now_seconds);
    void applyBindings(SceneRuntime& runtime);

    // Apply queued OSC particle bursts. `consumer_cursor` tracks the last applied
    // sequence so multiple consumers (e.g. one ExtGenesis session per GPU) can
    // each apply the same events without draining them for the others.
    std::uint32_t applyParticleEmits(SceneRuntime& runtime, std::uint64_t& consumer_cursor);
    // Apply queued room-person poses (pooled feet + wall; idle slots → origin).
    std::uint32_t applyRoomPerson(SceneRuntime& runtime, std::uint64_t& consumer_cursor);

    const std::vector<OscLearnedMessage>& learnedMessages() const { return learned_; }
    const std::vector<OscBinding>& bindings() const { return bindings_; }
    const std::vector<OscParticleEmitEvent>& particleEmitEvents() const { return particle_events_; }
    const std::vector<OscRoomPersonEvent>& roomPersonEvents() const { return room_person_events_; }
    // Occupied-slot wall-ray samples for editor debug draw (updated by applyRoomPerson).
    const std::array<OscWallRayDebugSample, kRoomPersonSlotCapacity>& wallRayDebugSamples() const {
        return wall_ray_debug_;
    }

    void clearLearned();
    void removeBinding(std::size_t index);
    void clearBindings();
    void clearParticleEmits();
    void clearRoomPersonEvents();

    // Incoming OSC debug log (ring buffer of recent packets / nested bundles).
    bool debugLogEnabled() const { return debug_log_enabled_; }
    void setDebugLogEnabled(bool enabled) { debug_log_enabled_ = enabled; }
    std::size_t debugLogCapacity() const { return debug_log_capacity_; }
    void setDebugLogCapacity(std::size_t capacity);
    const std::vector<OscDebugLogEntry>& debugLog() const { return debug_log_; }
    void clearDebugLog();
    std::string exportDebugLog() const;

    bool isAwaitingParameterLink() const { return pending_link_.has_value(); }
    const std::string& pendingLinkAddress() const { return pending_link_address_; }
    int pendingLinkArgIndex() const { return pending_link_arg_index_; }

    void beginLink(const std::string& address, int arg_index);
    void cancelLink();
    bool completeLink(OscParamTarget target, const std::string& node_path, std::uint32_t material_id = 0);

    std::string lastError() const { return last_error_; }
    std::uint64_t totalPackets() const { return total_packets_; }
    std::uint64_t totalMessages() const { return total_messages_; }
    std::uint64_t parseErrors() const { return parse_errors_; }
    std::uint64_t totalParticleEmits() const { return total_particle_emits_; }
    std::uint64_t totalRoomPersonUpdates() const { return total_room_person_updates_; }
    // Last TUIO fseq frame (optional debug). -1 = none yet.
    std::int32_t lastTuioFseq() const { return tuio_last_fseq_; }
    const std::vector<std::int32_t>& tuioAliveIds() const { return tuio_alive_ids_; }

private:
    void ingestMessage(const OscMessage& message, double now_seconds);
    void updateLearned(const OscMessage& message, double now_seconds);
    void storeLastValues(const OscMessage& message);
    void ingestParticleEmit(const OscMessage& message);
    void ingestRoomPerson(const OscMessage& message, double now_seconds);
    void ingestTuio(const OscMessage& message, double now_seconds);
    void pushRoomPersonEvent(OscRoomPersonEvent event, double now_seconds);
    void pushDebugEntries(std::vector<OscDebugLogEntry> entries, double now_seconds,
                          std::uint64_t packet_seq);
    static std::string formatWallClockNow();

    Settings settings_{};
    OscParticleEmitSettings particle_emit_{};
    OscRoomPersonSettings room_person_{};
    OscUdpReceiver receiver_;
    std::vector<OscLearnedMessage> learned_;
    std::vector<OscBinding> bindings_;
    std::vector<OscParticleEmitEvent> particle_events_;
    std::vector<OscRoomPersonEvent> room_person_events_;
    std::array<OscRoomPersonSlot, kRoomPersonSlotCapacity> room_person_slots_{};
    std::array<OscWallRayDebugSample, kRoomPersonSlotCapacity> wall_ray_debug_{};
    // TUIO alive pool (session IDs). When has_alive_, missing from_tuio slots park.
    bool tuio_has_alive_ = false;
    std::vector<std::int32_t> tuio_alive_ids_;
    std::int32_t tuio_last_fseq_ = -1;
    double last_poll_seconds_ = 0.0;
    std::unordered_map<std::string, std::vector<OscArg>> last_values_;

    struct PendingLink {
        std::string address;
        int arg_index = 0;
    };
    std::optional<PendingLink> pending_link_;
    std::string pending_link_address_;
    int pending_link_arg_index_ = 0;

    std::string last_error_;
    std::uint64_t total_packets_ = 0;
    std::uint64_t total_messages_ = 0;
    std::uint64_t parse_errors_ = 0;
    std::uint64_t next_particle_sequence_ = 1;
    std::uint64_t total_particle_emits_ = 0;
    std::uint64_t next_room_person_sequence_ = 1;
    std::uint64_t total_room_person_updates_ = 0;
    bool settings_dirty_ = false;

    bool debug_log_enabled_ = true;
    std::size_t debug_log_capacity_ = 512;
    std::vector<OscDebugLogEntry> debug_log_;
};

// Process-wide OSC hub for multi-stream hosts (ExtGenesis: one listen, N GPU sessions).
OscProtocol& sharedOscProtocol();

} // namespace genesis
