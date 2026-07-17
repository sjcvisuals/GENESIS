#pragma once

#include "protocol/OscTypes.hpp"
#include "protocol/OscUdpReceiver.hpp"
#include "scene/SceneRuntime.hpp"

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

    bool startListening(std::string* out_error = nullptr);
    void stopListening();
    bool isListening() const { return receiver_.isListening(); }

    void poll(double now_seconds);
    void applyBindings(SceneRuntime& runtime);

    const std::vector<OscLearnedMessage>& learnedMessages() const { return learned_; }
    const std::vector<OscBinding>& bindings() const { return bindings_; }

    void clearLearned();
    void removeBinding(std::size_t index);
    void clearBindings();

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

private:
    void ingestMessage(const OscMessage& message, double now_seconds);
    void updateLearned(const OscMessage& message, double now_seconds);
    void storeLastValues(const OscMessage& message);

    Settings settings_{};
    OscUdpReceiver receiver_;
    std::vector<OscLearnedMessage> learned_;
    std::vector<OscBinding> bindings_;
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
    bool settings_dirty_ = false;
};

} // namespace genesis
