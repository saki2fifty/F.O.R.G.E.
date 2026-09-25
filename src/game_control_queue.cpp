#include <deque>
#include <forge/game_control_queue.hpp>
#include <limits>
#include <set>
#include <thread>

namespace forge {
using Json = nlohmann::json;
struct GameControlQueue::State {
    std::thread::id owner = std::this_thread::get_id();
    bool alive = true, draining = false;
    std::uint64_t next = 0;
    Json status = Json::object();
    std::weak_ptr<Endpoint> active;
    struct Pending {
        std::weak_ptr<Endpoint> endpoint;
        std::uint64_t token;
    };
    std::deque<Pending> pending;
    // True while a pending entry is being polled. Reserves the in-flight
    // slot against the 256-entry bound so callbacks may enqueue new work.
    bool inflight = false;
    void check() const {
        if (!alive || owner != std::this_thread::get_id())
            throw std::runtime_error("Game service requires its live owner thread");
    }
    std::size_t outstanding() const { return pending.size() + (inflight ? 1 : 0); }
};
struct GameControlQueue::Endpoint final : GameControlService,
                                          std::enable_shared_from_this<Endpoint> {
    explicit Endpoint(const std::shared_ptr<State>& owner) : state(owner) {}
    std::weak_ptr<State> state;
    bool retired = false;
    struct Registration {
        std::shared_ptr<void> code; // Destroy callbacks before the code lease.
        GameSaveSchema schema;
    };
    struct Request {
        std::string module;
        Json command;
        GameRequestStatus status{"queued"};
    };
    std::map<std::string, std::shared_ptr<Registration>> schemas;
    std::set<std::string> revoked;
    std::map<std::uint64_t, Request> requests;
    std::shared_ptr<State> check(bool active = false) const {
        auto owner = state.lock();
        if (!owner || retired)
            throw std::runtime_error("Game service scope has retired");
        owner->check();
        if (active && owner->active.lock().get() != this)
            throw std::runtime_error("Only the active world can request game operations");
        return owner;
    }
    Json query() const override { return check()->status; }
    std::uint64_t request(const std::string& module, const Json& command) override {
        auto owner = check(true);
        if (revoked.contains(module))
            throw std::runtime_error("Game service module scope has retired");
        if (module.empty() || module.size() > 128 || !command.is_object() ||
            command.dump().size() > 1024 * 1024)
            throw std::runtime_error("Invalid game request owner or payload");
        static const std::set<std::string> operations{
            "pause",        "resume",        "prepare",       "activate",       "cancel",
            "unload",       "quit",          "save",          "load",           "slots",
            "erase",        "settings",      "set_settings",  "input_contexts", "cursor",
            "rebind_begin", "rebind_cancel", "rebind_commit", "rebind_reset",   "ui_navigation"};
        if (!operations.contains(command.at("operation").get<std::string>()))
            throw std::runtime_error("Unknown game operation");
        if (requests.size() >= 128 || owner->outstanding() >= 256)
            throw std::runtime_error("Game request queue is full; release completed requests");
        if (owner->next == std::numeric_limits<std::uint64_t>::max())
            throw std::overflow_error("Game request token space exhausted");
        const auto token = ++owner->next;
        requests.emplace(token, Request{module, command});
        try {
            owner->pending.push_back({weak_from_this(), token});
        } catch (...) {
            requests.erase(token);
            throw;
        }
        return token;
    }
    GameRequestStatus inspect(const std::string& module, std::uint64_t token) const override {
        check();
        const auto found = requests.find(token);
        if (found == requests.end() || found->second.module != module)
            throw std::runtime_error("Stale or foreign game request token");
        return found->second.status;
    }
    void release(const std::string& module, std::uint64_t token) override {
        (void)inspect(module, token);
        requests.erase(token);
    }
    void save_schema(const std::string& module, GameSaveSchema schema,
                     std::shared_ptr<void> code) override {
        check();
        if (revoked.contains(module))
            throw std::runtime_error("Game service module scope has retired");
        if (module.empty() || module.size() > 128 || !schema.validate || !schema.version ||
            schema.version > 1000000 || schema.migrations.size() > 128)
            throw std::runtime_error("Invalid game save schema registration");
        for (const auto& [version, callback] : schema.migrations)
            if (!version || version >= schema.version || !callback)
                throw std::runtime_error("Invalid game save migration registration");
        if (schemas.contains(module) || schemas.size() >= 32)
            throw std::runtime_error("Module already registered a save schema");
        schemas.emplace(module, std::make_shared<Registration>(
                                    Registration{std::move(code), std::move(schema)}));
    }
    void revoke(const std::string& module) override {
        if (auto owner = state.lock()) {
            owner->check();
            if (retired)
                return;
        }
        check();
        revoked.insert(module);
        std::erase_if(requests, [&](const auto& entry) { return entry.second.module == module; });
        schemas.erase(module);
    }
};
GameControlQueue::GameControlQueue() : state_(std::make_shared<State>()) {}
GameControlQueue::~GameControlQueue() { state_->alive = false; }
std::shared_ptr<GameControlService> GameControlQueue::world() {
    state_->check();
    return std::make_shared<Endpoint>(state_);
}
EngineModule GameControlQueue::module() {
    state_->check();
    EngineModule module;
    module.id = "forge.game";
    module.runtime_roles = role_mask(WorldRole::Runtime);
    module.allowed_services = capability(Capability::Game);
    module.provided_services = capability(Capability::Game);
    const std::weak_ptr<State> weak = state_;
    module.start = [weak](ModuleContext& context) {
        auto owner = weak.lock();
        if (!owner)
            throw std::runtime_error("Game service host has expired");
        owner->check();
        auto endpoint = std::make_shared<Endpoint>(owner);
        context.services.publish_game(endpoint);
        context.state = std::move(endpoint);
    };
    module.stop = [](ModuleContext& context) {
        if (auto endpoint = std::static_pointer_cast<Endpoint>(context.state)) {
            endpoint->retired = true;
            endpoint->requests.clear();
            endpoint->schemas.clear();
        }
        context.services.publish_game({});
    };
    return module;
}
void GameControlQueue::activate(const std::shared_ptr<GameControlService>& service) {
    state_->check();
    auto endpoint = std::dynamic_pointer_cast<Endpoint>(service);
    if (!endpoint || endpoint->state.lock() != state_ || endpoint->retired)
        throw std::runtime_error("Cannot activate foreign or retired game service");
    if (auto old = state_->active.lock(); old && old != endpoint)
        retire(old);
    state_->active = endpoint;
}
void GameControlQueue::retire(const std::shared_ptr<GameControlService>& service) {
    state_->check();
    auto endpoint = std::dynamic_pointer_cast<Endpoint>(service);
    if (!endpoint || endpoint->state.lock() != state_)
        throw std::runtime_error("Cannot retire foreign game service");
    endpoint->retired = true;
    endpoint->requests.clear();
    endpoint->schemas.clear();
}
void GameControlQueue::status(Json copied) {
    state_->check();
    if (!copied.is_object() || copied.dump().size() > 1024 * 1024)
        throw std::runtime_error("Game service status must be a bounded object");
    state_->status = std::move(copied);
}
bool GameControlQueue::pending(std::uint64_t token) const {
    state_->check();
    auto active = state_->active.lock();
    if (!active || active->retired)
        return false;
    auto found = active->requests.find(token);
    if (found == active->requests.end())
        return false;
    // The contract: a queued command is pending while its request status is
    // "queued" and the module has not been revoked. Completed/released/
    // revoked/foreign-world tokens are not pending.
    if (active->revoked.contains(found->second.module))
        return false;
    return found->second.status.state == "queued";
}
namespace {
// RAII guard that flips the draining flag back off when the dispatch scope
// ends, even if an exception escapes (the queue re-publishes failures as
// receipts rather than letting them propagate).
struct DrainGuard {
    bool& busy;
    ~DrainGuard() { busy = false; }
};
} // namespace
void GameControlQueue::poll(const Poll& poll_fn) {
    state_->check();
    if (state_->draining || !poll_fn)
        throw std::logic_error("Game service poll cannot nest or use empty callback");
    DrainGuard guard{state_->draining};
    state_->draining = true;
    // Reserve the in-flight slot for the current pending entry so callbacks
    // may enqueue new work without exceeding the 256-entry bound. The slot
    // is released before any requeue so the bound is never violated
    // transiently. At most one entry is in-flight because each pending
    // entry runs at most once per host call.
    auto snapshot = state_->pending.size();
    while (snapshot--) {
        if (state_->inflight)
            break;
        auto entry = state_->pending.front();
        state_->pending.pop_front();
        state_->inflight = true;
        struct InflightReset {
            bool& slot;
            ~InflightReset() { slot = false; }
        } reset{state_->inflight};
        auto endpoint = entry.endpoint.lock();
        if (!endpoint || endpoint->retired || state_->active.lock().get() != endpoint.get())
            continue;
        auto request = endpoint->requests.find(entry.token);
        if (request == endpoint->requests.end())
            continue;
        const auto command = request->second.command;
        const auto module = request->second.module;
        const auto schema = endpoint->schemas.find(module);
        // Keep callbacks AND their code alive across executor-driven retirement.
        const auto registration = schema == endpoint->schemas.end() ? nullptr : schema->second;
        std::optional<Json> result;
        // Validate result payload inside the same try that invokes the
        // callback. A callback returning an invalid JSON value (e.g. one
        // containing malformed UTF-8) must not throw out of the host pump
        // and lose the receipt; serialization is caught and converted to
        // a failed receipt alongside any other callback exception.
        try {
            result = poll_fn(entry.token, command, registration ? &registration->schema : nullptr,
                             module);
            if (result && result->dump().size() > 1024 * 1024)
                throw std::runtime_error("Game operation result exceeds 1 MiB");
        } catch (const std::exception& e) {
            if (!endpoint->retired) {
                auto it = endpoint->requests.find(entry.token);
                if (it != endpoint->requests.end()) {
                    GameRequestStatus status;
                    status.state = "failed";
                    status.error = std::string(e.what()).substr(0, 8192);
                    it->second.status = std::move(status);
                }
            }
            continue;
        } catch (...) {
            if (!endpoint->retired) {
                auto it = endpoint->requests.find(entry.token);
                if (it != endpoint->requests.end()) {
                    GameRequestStatus status;
                    status.state = "failed";
                    status.error = "Game operation callback failed";
                    it->second.status = std::move(status);
                }
            }
            continue;
        }
        // The executor can publish another world. Never use an iterator
        // retained across that boundary or publish a receipt into a
        // retired scope. Release the in-flight slot before any requeue so
        // the 256 bound is never violated transiently.
        if (!result) {
            state_->inflight = false;
            if (!endpoint->retired && state_->active.lock().get() == endpoint.get() &&
                endpoint->requests.contains(entry.token) && !endpoint->revoked.contains(module))
                state_->pending.push_back(entry);
            continue;
        }
        if (!endpoint->retired) {
            auto it = endpoint->requests.find(entry.token);
            if (it != endpoint->requests.end()) {
                GameRequestStatus status;
                status.state = "succeeded";
                status.value = std::move(*result);
                it->second.status = std::move(status);
            }
        }
    }
}
void GameControlQueue::drain(const Execute& execute) {
    if (!execute)
        throw std::logic_error("Game service drain requires a non-empty callback");
    poll([&execute](std::uint64_t, const Json& command, const GameSaveSchema* schema,
                    const std::string& module) -> std::optional<Json> {
        return std::optional<Json>(execute(command, schema, module));
    });
}
} // namespace forge
