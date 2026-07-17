#include "protocol/OscUdpReceiver.hpp"

#include <array>
#include <string>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace genesis {

namespace {

#if defined(_WIN32)
using SocketHandle = SOCKET;
constexpr SocketHandle kInvalidSocket = INVALID_SOCKET;
#else
using SocketHandle = int;
constexpr SocketHandle kInvalidSocket = -1;
#endif

void closeSocket(SocketHandle socket) {
    if (socket == kInvalidSocket) {
        return;
    }
#if defined(_WIN32)
    closesocket(socket);
#else
    close(socket);
#endif
}

bool setNonBlocking(SocketHandle socket) {
#if defined(_WIN32)
    u_long mode = 1;
    return ioctlsocket(socket, FIONBIO, &mode) == 0;
#else
    const int flags = fcntl(socket, F_GETFL, 0);
    if (flags < 0) {
        return false;
    }
    return fcntl(socket, F_SETFL, flags | O_NONBLOCK) == 0;
#endif
}

} // namespace

OscUdpReceiver::OscUdpReceiver() {
#if defined(_WIN32)
    WSADATA wsa{};
    wsa_started_ = WSAStartup(MAKEWORD(2, 2), &wsa) == 0;
#endif
}

OscUdpReceiver::~OscUdpReceiver() {
    stop();
#if defined(_WIN32)
    if (wsa_started_) {
        WSACleanup();
    }
#endif
}

bool OscUdpReceiver::start(const std::string& bind_ip, std::uint16_t port, std::string* out_error) {
    stop();

#if defined(_WIN32)
    if (!wsa_started_) {
        if (out_error) *out_error = "Winsock initialization failed";
        return false;
    }
#endif

    const SocketHandle socket =
#if defined(_WIN32)
        ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
#else
        ::socket(AF_INET, SOCK_DGRAM, 0);
#endif
    if (socket == kInvalidSocket) {
#if defined(_WIN32)
        if (out_error) {
            *out_error = "Failed to create UDP socket (WSA " + std::to_string(WSAGetLastError()) + ")";
        }
#else
        if (out_error) *out_error = "Failed to create UDP socket";
#endif
        return false;
    }

    const int reuse = 1;
    setsockopt(socket, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuse), sizeof(reuse));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);

    const std::string ip = bind_ip.empty() ? "0.0.0.0" : bind_ip;
#if defined(_WIN32)
    if (InetPtonA(AF_INET, ip.c_str(), &addr.sin_addr) != 1) {
#else
    if (inet_pton(AF_INET, ip.c_str(), &addr.sin_addr) != 1) {
#endif
        closeSocket(socket);
        if (out_error) *out_error = "Invalid bind IP: " + ip;
        return false;
    }

    if (bind(socket, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        closeSocket(socket);
        if (out_error) {
#if defined(_WIN32)
            *out_error = "Failed to bind UDP socket on " + ip + ":" + std::to_string(port) + " (WSA " +
                         std::to_string(WSAGetLastError()) +
                         "). Use 0.0.0.0 to listen on all local interfaces.";
#else
            *out_error = "Failed to bind UDP socket on " + ip + ":" + std::to_string(port) +
                         ". Use 0.0.0.0 to listen on all local interfaces.";
#endif
        }
        return false;
    }

    if (!setNonBlocking(socket)) {
        closeSocket(socket);
        if (out_error) *out_error = "Failed to set UDP socket non-blocking";
        return false;
    }

#if defined(_WIN32)
    socket_ = static_cast<std::uintptr_t>(socket);
#else
    socket_ = socket;
#endif
    bind_ip_ = ip;
    port_ = port;
    listening_ = true;
    return true;
}

void OscUdpReceiver::stop() {
    if (!listening_) {
        return;
    }
#if defined(_WIN32)
    SocketHandle socket = static_cast<SocketHandle>(socket_);
    closeSocket(socket);
    socket_ = static_cast<std::uintptr_t>(kInvalidSocket);
#else
    closeSocket(socket_);
    socket_ = kInvalidSocket;
#endif
    listening_ = false;
}

int OscUdpReceiver::poll(std::vector<std::vector<std::uint8_t>>& out_packets) {
    if (!listening_) {
        return 0;
    }

#if defined(_WIN32)
    const SocketHandle socket = static_cast<SocketHandle>(socket_);
#else
    const SocketHandle socket = socket_;
#endif
    if (socket == kInvalidSocket) {
        return 0;
    }

    int packet_count = 0;
    for (;;) {
        std::array<std::uint8_t, 4096> buffer{};
        const int received = recvfrom(socket, reinterpret_cast<char*>(buffer.data()),
                                      static_cast<int>(buffer.size()), 0, nullptr, nullptr);
        if (received <= 0) {
            break;
        }
        out_packets.emplace_back(buffer.begin(), buffer.begin() + received);
        ++packet_count;
        if (packet_count > 64) {
            break;
        }
    }
    return packet_count;
}

} // namespace genesis
