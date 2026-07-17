#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace genesis {

class OscUdpReceiver {
public:
    OscUdpReceiver();
    ~OscUdpReceiver();

    OscUdpReceiver(const OscUdpReceiver&) = delete;
    OscUdpReceiver& operator=(const OscUdpReceiver&) = delete;

    bool start(const std::string& bind_ip, std::uint16_t port, std::string* out_error = nullptr);
    void stop();
    bool isListening() const { return listening_; }

    // Non-blocking receive. Returns number of packets read this call.
    int poll(std::vector<std::vector<std::uint8_t>>& out_packets);

    std::string bindIp() const { return bind_ip_; }
    std::uint16_t port() const { return port_; }

private:
    bool listening_ = false;
    std::string bind_ip_;
    std::uint16_t port_ = 0;
#if defined(_WIN32)
    std::uintptr_t socket_ = static_cast<std::uintptr_t>(-1);
    bool wsa_started_ = false;
#else
    int socket_ = -1;
#endif
};

} // namespace genesis
