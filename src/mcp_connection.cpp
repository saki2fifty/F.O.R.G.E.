#include "mcp.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
// clang-format off
#include <winsock2.h>
#include <ws2tcpip.h>
// clang-format on
#else
#include <cerrno>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace forge {
namespace {
using Json = nlohmann::json;
using Clock = std::chrono::steady_clock;
#ifdef _WIN32
struct Network {
    Network() {
        WSADATA data{};
        if (WSAStartup(MAKEWORD(2, 2), &data) != 0)
            throw std::runtime_error("Cannot initialize local MCP networking");
    }
    ~Network() { WSACleanup(); }
};
using Socket = SOCKET;
constexpr auto invalid_socket = INVALID_SOCKET;
bool pending() {
    const auto error = WSAGetLastError();
    return error == WSAEWOULDBLOCK || error == WSAEINPROGRESS || error == WSAEINTR;
}
#else
struct Network {};
using Socket = int;
constexpr auto invalid_socket = -1;
bool pending() { return errno == EINPROGRESS || errno == EAGAIN || errno == EINTR; }
#endif
struct Channel {
    Socket socket = invalid_socket;
    ~Channel() {
        if (socket == invalid_socket)
            return;
#ifdef _WIN32
        closesocket(socket);
#else
        close(socket);
#endif
    }
};
void wait(Socket socket, bool writing, Clock::time_point deadline,
          const std::atomic_bool& cancelled) {
    while (true) {
        if (cancelled)
            throw std::runtime_error("Request cancelled; committed edits remain applied");
        if (Clock::now() >= deadline)
            throw std::runtime_error(
                "Editor request timed out; inspect state or retrieve the same sequence receipt");
#ifdef _WIN32
        fd_set ready;
        FD_ZERO(&ready);
        FD_SET(socket, &ready);
        auto failed = ready;
        timeval timeout{0, 50000};
        const auto count =
            select(0, writing ? nullptr : &ready, writing ? &ready : nullptr, &failed, &timeout);
#else
        pollfd ready{socket, short(writing ? POLLOUT : POLLIN), 0};
        const auto count = poll(&ready, 1, 50);
#endif
        if (count > 0)
            return;
        if (count < 0 && !pending())
            throw std::runtime_error("Local editor connection failed");
    }
}
} // namespace

Json mcp_editor_request(const Json& connection, const Json& request, const Json& sequence,
                        const std::atomic_bool& cancelled) {
    [[maybe_unused]] Network network;
    Channel channel;
    channel.socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (channel.socket == invalid_socket)
        throw std::runtime_error("Cannot create local editor connection");
#ifdef _WIN32
    u_long nonblocking = 1;
    if (ioctlsocket(channel.socket, FIONBIO, &nonblocking) != 0 ||
        !SetHandleInformation(reinterpret_cast<HANDLE>(channel.socket), HANDLE_FLAG_INHERIT, 0))
        throw std::runtime_error("Cannot configure local editor connection");
#else
    if (fcntl(channel.socket, F_SETFL, O_NONBLOCK) < 0 ||
        fcntl(channel.socket, F_SETFD, FD_CLOEXEC) < 0)
        throw std::runtime_error("Cannot configure local editor connection");
#endif
    const auto deadline = Clock::now() + std::chrono::seconds(12);
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(connection.at("port").get<std::uint16_t>());
    if (connect(channel.socket, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0) {
        if (!pending())
            throw std::runtime_error("Editor connection unavailable; enable a new connection");
        wait(channel.socket, true, deadline, cancelled);
        int error = 0;
#ifdef _WIN32
        int size = sizeof(error);
#else
        socklen_t size = sizeof(error);
#endif
        if (getsockopt(channel.socket, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&error),
                       &size) != 0 ||
            error != 0)
            throw std::runtime_error("Editor connection unavailable; enable a new connection");
    }
    Json envelope = {
        {"transport", 1}, {"id", "mcp"}, {"token", connection.at("token")}, {"request", request}};
    if (!sequence.is_null())
        envelope["sequence"] = sequence;
    const auto wire = envelope.dump() + '\n';
    if (wire.size() > 1048577)
        throw std::runtime_error("Editor request exceeds 1 MiB");
    std::size_t sent = 0;
    while (sent < wire.size()) {
        wait(channel.socket, true, deadline, cancelled);
#ifdef _WIN32
        constexpr int flags = 0;
#else
        constexpr int flags = MSG_NOSIGNAL;
#endif
        const auto count = send(channel.socket, wire.data() + sent, int(wire.size() - sent), flags);
        if (count < 0 && pending())
            continue;
        if (count <= 0)
            throw std::runtime_error("Editor send failed; do not blindly repeat an edit");
        sent += std::size_t(count);
    }
    std::string response;
    std::array<char, 65536> buffer{};
    while (true) {
        wait(channel.socket, false, deadline, cancelled);
        const auto count = recv(channel.socket, buffer.data(), int(buffer.size()), 0);
        if (count < 0 && pending())
            continue;
        if (count <= 0)
            throw std::runtime_error("Editor disconnected; inspect state before repeating an edit");
        response.append(buffer.data(), std::size_t(count));
        if (response.size() > 16777216)
            throw std::runtime_error("Editor response exceeds 16 MiB; use paged queries");
        if (response.find('\n') != std::string::npos)
            break;
    }
    Json result;
    try {
        result = Json::parse(response, [](int depth, Json::parse_event_t, Json&) {
            if (depth > 128)
                throw std::runtime_error("Editor response is too deeply nested");
            return true;
        });
        if (result.at("transport") != 1 || result.at("id") != "mcp" ||
            !result.at("response").is_object() || !result.at("next_sequence").is_number_unsigned())
            throw std::runtime_error("Invalid editor response");
    } catch (const std::exception&) {
        throw std::runtime_error("Malformed editor response; inspect state before retrying");
    }
    auto authored = result.at("response");
    authored["next_sequence"] = result.at("next_sequence");
    return authored;
}
} // namespace forge
