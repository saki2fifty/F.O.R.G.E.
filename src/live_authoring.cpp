#include <algorithm>
#include <array>
#include <chrono>
#include <deque>
#include <forge/live_authoring.hpp>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
// Winsock must precede headers that require Win32 types.
// clang-format off
#include <winsock2.h>
#include <ws2tcpip.h>
#include <bcrypt.h>
// clang-format on
#else
#include <cerrno>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/random.h>
#include <sys/socket.h>
#include <unistd.h>
#endif
namespace forge {
namespace {
#ifdef _WIN32
using Socket = SOCKET;
constexpr Socket invalid_socket = INVALID_SOCKET;
void close_socket(Socket value) { closesocket(value); }
bool would_block() { return WSAGetLastError() == WSAEWOULDBLOCK; }
void configure(Socket value) {
    u_long enabled = 1;
    if (ioctlsocket(value, FIONBIO, &enabled) != 0 ||
        !SetHandleInformation(reinterpret_cast<HANDLE>(value), HANDLE_FLAG_INHERIT, 0))
        throw std::runtime_error("Cannot configure nonblocking automation socket");
}
#else
using Socket = int;
constexpr Socket invalid_socket = -1;
void close_socket(Socket value) { close(value); }
bool would_block() { return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR; }
void configure(Socket value) {
    if (fcntl(value, F_SETFL, O_NONBLOCK) < 0 || fcntl(value, F_SETFD, FD_CLOEXEC) < 0)
        throw std::runtime_error("Cannot configure nonblocking automation socket");
}
#endif
std::string secret() {
    std::array<unsigned char, 32> bytes{};
#ifdef _WIN32
    if (BCryptGenRandom(nullptr, bytes.data(), static_cast<ULONG>(bytes.size()),
                        BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0)
        throw std::runtime_error("Cannot generate automation access token");
#else
    std::size_t offset = 0;
    while (offset < bytes.size()) {
        const auto count = getrandom(bytes.data() + offset, bytes.size() - offset, 0);
        if (count < 0 && errno == EINTR)
            continue;
        if (count <= 0)
            throw std::runtime_error("Cannot generate automation access token");
        offset += static_cast<std::size_t>(count);
    }
#endif
    std::string result;
    for (auto byte : bytes) {
        result += "0123456789abcdef"[byte >> 4];
        result += "0123456789abcdef"[byte & 15];
    }
    return result;
}
bool matches(const std::string& a, const std::string& b) {
    if (a.size() != b.size())
        return false;
    unsigned diff = 0;
    for (std::size_t i = 0; i < a.size(); ++i)
        diff |= unsigned(a[i] ^ b[i]);
    return diff == 0;
}
Json failure(const char* code, const std::string& message) {
    return {{"api", 1}, {"ok", false}, {"error", {{"code", code}, {"message", message}}}};
}
bool mutation(const std::string& method) {
    return method == "scene.apply" || method == "scene.replace" || method == "history.undo" ||
           method == "history.redo";
}
} // namespace
struct LiveAuthoring::State {
    Socket listener = invalid_socket, client = invalid_socket;
    std::unique_ptr<AuthoringSession> session;
    std::thread::id owner;
    bool edits = false;
    std::uint16_t port = 0;
    std::string token, input, output, status = "Automation is off";
    std::size_t sent = 0;
    std::chrono::steady_clock::time_point deadline;
    std::uint64_t next = 1;
    struct Receipt {
        std::uint64_t sequence;
        Json request, response;
        std::size_t bytes;
    };
    std::deque<Receipt> receipts;
    std::size_t receipt_bytes = 0;
#ifdef _WIN32
    bool winsock = false;
#endif
    void disconnect() {
        if (client != invalid_socket)
            close_socket(client);
        client = invalid_socket;
        input.clear();
        output.clear();
        sent = 0;
    }
    ~State() {
        disconnect();
        if (listener != invalid_socket)
            close_socket(listener);
#ifdef _WIN32
        if (winsock)
            WSACleanup();
#endif
    }
    Json dispatch(const Json& wire, const std::string& busy) {
        Json response;
        std::string id;
        try {
            if (wire.at("transport") != 1)
                throw std::runtime_error("Expected live transport 1");
            id = wire.at("id").get<std::string>();
            if (id.empty() || id.size() > 64) {
                id.clear();
                throw std::runtime_error("Request id must be 1..64 bytes");
            }
            if (!matches(wire.at("token").get<std::string>(), token))
                response = failure("unauthorized", "Invalid access token");
            else {
                const auto& request = wire.at("request");
                const auto method = request.at("method").get<std::string>();
                if (mutation(method)) {
                    if (!edits)
                        response = failure("capability_denied", "Connection permits reading only");
                    else if (!wire.at("sequence").is_number_unsigned() &&
                             !(wire.at("sequence").is_number_integer() &&
                               wire.at("sequence").get<std::int64_t>() > 0))
                        response =
                            failure("invalid_sequence", "Expected a positive mutation sequence");
                    else {
                        const auto sequence = wire.at("sequence").get<std::uint64_t>();
                        const auto receipt = std::find_if(
                            receipts.begin(), receipts.end(),
                            [&](const Receipt& item) { return item.sequence == sequence; });
                        if (receipt != receipts.end())
                            response = receipt->request == request
                                           ? receipt->response
                                           : failure("sequence_conflict",
                                                     "Sequence was used for a different request");
                        else if (sequence < next)
                            response = failure(
                                "receipt_expired",
                                "Old sequence cannot be executed again; read current state");
                        else if (sequence != next)
                            response =
                                failure("invalid_sequence", "Discover the next mutation sequence");
                        else {
                            // Consume before mutation. Even failure to retain a receipt must
                            // never permit this sequence to execute again.
                            ++next;
                            response =
                                busy.empty() ? session->handle(request) : failure("busy", busy);
                            const auto bytes = request.dump().size() + response.dump().size();
                            receipts.push_back({sequence, request, response, bytes});
                            receipt_bytes += bytes;
                            while (receipts.size() > 32 || receipt_bytes > 8 * 1024 * 1024) {
                                receipt_bytes -= receipts.front().bytes;
                                receipts.pop_front();
                            }
                        }
                    }
                } else if (method == "discover" || method == "scene.read" ||
                           method == "entity.query" || method == "scene.diagnostics") {
                    response = session->handle(request);
                    if (method == "discover" && response.value("ok", false)) {
                        auto& result = response["result"];
                        result["persistence"] = "editor-document; save through editor";
                        result["capabilities"] = edits ? Json{"scene.read", "scene.edit", "history"}
                                                       : Json{"scene.read"};
                        result["mutation_available"] = edits && busy.empty();
                        result["unavailable_reason"] = !edits ? "Read-only connection" : busy;
                        if (!edits) {
                            result["commands"] = Json::array();
                            result["methods"] = {"discover", "scene.read", "entity.query",
                                                 "scene.diagnostics"};
                        }
                        result["transport_limits"] = {
                            {"request_bytes", 1048576}, {"response_bytes", 16777216},
                            {"request_depth", 64},      {"clients", 1},
                            {"receipt_count", 32},      {"receipt_bytes", 8388608}};
                    }
                } else {
                    response =
                        failure("unknown_method", "Method is not exposed by live transport 1");
                }
            }
        } catch (const std::exception&) {
            // Avoid echoing arbitrary request contents (including credentials) into diagnostics.
            response = failure("invalid_request", "Malformed live request envelope");
        }
        status = response.value("ok", false)
                     ? "Last request completed"
                     : "Last request: " + response.at("error").at("code").get<std::string>();
        return {{"transport", 1}, {"id", id}, {"response", response}, {"next_sequence", next}};
    }
};
LiveAuthoring::LiveAuthoring() : state_(std::make_unique<State>()) {}
LiveAuthoring::~LiveAuthoring() = default;
void LiveAuthoring::start(Scene& scene, bool allow_edits) {
    auto candidate = std::make_unique<State>();
    candidate->owner = std::this_thread::get_id();
    candidate->token = secret();
    candidate->edits = allow_edits;
    candidate->session = std::make_unique<AuthoringSession>(scene);
#ifdef _WIN32
    WSADATA data{};
    if (WSAStartup(MAKEWORD(2, 2), &data) != 0)
        throw std::runtime_error("Cannot initialize Winsock");
    candidate->winsock = true;
#endif
    candidate->listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (candidate->listener == invalid_socket)
        throw std::runtime_error("Cannot create automation listener");
    configure(candidate->listener);
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = 0;
    if (bind(candidate->listener, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0 ||
        listen(candidate->listener, 1) != 0)
        throw std::runtime_error("Cannot bind loopback automation listener");
#ifdef _WIN32
    int length = sizeof(address);
#else
    socklen_t length = sizeof(address);
#endif
    if (getsockname(candidate->listener, reinterpret_cast<sockaddr*>(&address), &length) != 0)
        throw std::runtime_error("Cannot query automation port");
    candidate->port = ntohs(address.sin_port);
    candidate->status = allow_edits ? "Listening: scene edits allowed" : "Listening: read only";
    state_ = std::move(candidate);
}
void LiveAuthoring::stop() { state_ = std::make_unique<State>(); }
bool LiveAuthoring::active() const { return state_->listener != invalid_socket; }
bool LiveAuthoring::editable() const { return active() && state_->edits; }
std::uint16_t LiveAuthoring::port() const { return state_->port; }
Json LiveAuthoring::connection() const {
    if (!active())
        throw std::runtime_error("Automation is off");
    return {
        {"transport", 1}, {"host", "127.0.0.1"}, {"port", state_->port}, {"token", state_->token}};
}
const std::string& LiveAuthoring::status() const { return state_->status; }
void LiveAuthoring::pump(const std::string& unavailable_reason) {
    auto& s = *state_;
    if (!active())
        return;
    if (std::this_thread::get_id() != s.owner)
        throw std::runtime_error("Pump automation on its owning thread");
    const auto now = std::chrono::steady_clock::now();
    if (s.client != invalid_socket && now >= s.deadline) {
        s.disconnect();
        s.status = "Client timed out; committed commands remain applied";
    }
    if (s.client == invalid_socket) {
        s.client = accept(s.listener, nullptr, nullptr);
        if (s.client == invalid_socket)
            return;
        try {
            configure(s.client);
        } catch (...) {
            s.disconnect();
            return;
        }
        s.deadline = now + std::chrono::seconds(10);
    }
    // Bound transport work per frame. One request/response per connection, no pipelining.
    if (s.output.empty()) {
        std::array<char, 65536> buffer{};
        const auto count = recv(s.client, buffer.data(), static_cast<int>(buffer.size()), 0);
        if (count == 0 || (count < 0 && !would_block())) {
            s.disconnect();
            return;
        }
        if (count < 0)
            return;
        s.input.append(buffer.data(), static_cast<std::size_t>(count));
        const auto newline = s.input.find('\n');
        if (s.input.size() > 1048576 + 1) {
            s.status = "Rejected request exceeding 1 MiB";
            s.disconnect();
            return;
        }
        if (newline == std::string::npos)
            return;
        if (newline != s.input.size() - 1) {
            s.status = "Rejected pipelined request";
            s.disconnect();
            return;
        }
        try {
            const auto wire = Json::parse(s.input, [](int depth, Json::parse_event_t, Json&) {
                if (depth > 64)
                    throw std::runtime_error("Request too deep");
                return true;
            });
            s.output = s.dispatch(wire, unavailable_reason).dump() + "\n";
        } catch (const std::exception&) {
            s.output = Json{{"transport", 1},
                            {"id", ""},
                            {"response",
                             failure("invalid_request", "Malformed JSON or nesting exceeds 64")}}
                           .dump() +
                       "\n";
            s.status = "Rejected malformed request";
        }
        if (s.output.size() > 16777216) {
            s.output = Json{{"transport", 1},
                            {"id", ""},
                            {"response", failure("limit_exceeded",
                                                 "Response exceeds 16 MiB; use paged queries")}}
                           .dump() +
                       "\n";
        }
    }
#ifdef _WIN32
    constexpr int flags = 0;
#else
    constexpr int flags = MSG_NOSIGNAL;
#endif
    const auto count =
        send(s.client, s.output.data() + s.sent,
             static_cast<int>(std::min<std::size_t>(65536, s.output.size() - s.sent)), flags);
    if (count <= 0) {
        if (count == 0 || !would_block())
            s.disconnect();
        return;
    }
    s.sent += static_cast<std::size_t>(count);
    if (s.sent == s.output.size())
        s.disconnect();
}
} // namespace forge
