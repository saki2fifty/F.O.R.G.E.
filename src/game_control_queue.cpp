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
    void check() const {
        if (!alive || owner != std::this_thread::get_id())
            throw std::runtime_error("Game service requires its live owner thread");
    }
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
        if (requests.size() >= 128 || owner->pending.size() >= 256)
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
void GameControlQueue::drain(const Execute& execute) {
    state_->check();
    if (state_->draining || !execute)
        throw std::logic_error("Game service drain cannot nest");
    struct Guard {
        bool& busy;
        ~Guard() { busy = false; }
    } guard{state_->draining};
    state_->draining = true;
    auto count = state_->pending.size();
    while (count--) {
        auto pending = state_->pending.front();
        state_->pending.pop_front();
        auto endpoint = pending.endpoint.lock();
        if (!endpoint || endpoint->retired || state_->active.lock() != endpoint)
            continue;
        auto request = endpoint->requests.find(pending.token);
        if (request == endpoint->requests.end())
            continue;
        const auto command = request->second.command;
        const auto module = request->second.module;
        const auto schema = endpoint->schemas.find(module);
        // Keep callbacks AND their code alive across executor-driven retirement.
        const auto registration = schema == endpoint->schemas.end() ? nullptr : schema->second;
        GameRequestStatus result;
        try {
            result.value = execute(command, registration ? &registration->schema : nullptr, module);
            if (result.value.dump().size() > 1024 * 1024)
                throw std::runtime_error("Game operation result exceeds 1 MiB");
            result.state = "succeeded";
        } catch (const std::exception& e) {
            result.state = "failed";
            result.error = std::string(e.what()).substr(0, 8192);
        } catch (...) {
            result.state = "failed";
            result.error = "Game operation callback failed";
        }
        // The executor can publish another world. Never use an iterator retained
        // across that boundary or publish a receipt into a retired scope.
        if (!endpoint->retired) {
            request = endpoint->requests.find(pending.token);
            if (request != endpoint->requests.end())
                request->second.status = std::move(result);
        }
    }
}
} // namespace forge
