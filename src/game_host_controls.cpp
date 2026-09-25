#include <algorithm>
#include <exception>
#include <forge/game_content.hpp>
#include <forge/game_host_controls.hpp>
#include <iostream>

namespace forge {
GameHostControls::GameHostControls(GameSession& game, std::shared_ptr<GameControlQueue> queue,
                                   GameStorage& storage, std::filesystem::path content,
                                   Json defaults, InputMap project_input, Json user,
                                   GamePlatformControls platform)
    : game_(game), queue_(std::move(queue)), storage_(storage), content_(std::move(content)),
      defaults_(std::move(defaults)), user_(std::move(user)),
      settings_(resolve_game_settings(defaults_, user_, project_input)),
      project_input_(std::move(project_input)), platform_(std::move(platform)) {
    if (!queue_)
        throw std::invalid_argument("Game controls require the session's request queue");
    // Polled cursor capture owns a delayed physical capture. The host's
    // synchronous release_cursor() path must still be able to force a
    // physical release on pause/unload, so configuring a polled cursor
    // requires a synchronous cursor callback as well. Navigation-only
    // polled adapters do not own capture/release and need no synchronous
    // cursor. This is not an IPC contract — adapters only own idempotency
    // and cancellation within a single host session.
    if (platform_.cursor_poll && !platform_.cursor)
        throw std::invalid_argument(
            "Game controls with a polled cursor must also configure a synchronous cursor");
    publish();
}
void GameHostControls::release_cursor() {
    // Synchronous release. Polled adapters are NOT consulted here; they own
    // their own idempotency and cancellation. The host cannot assume that a
    // delayed backend ack for a queued polled request has actually taken
    // effect at the native side — callers must wait for the adapter to
    // return true on the next pump before trusting the captured state. We
    // only flip the local cursor_ flag so the host state stays consistent.
    if (platform_.cursor)
        platform_.cursor(false);
    cursor_ = false;
    const auto state = game_.status().at("state").get<std::string>();
    if (state == "running" || state == "paused")
        game_.input({{{}, 0, true}});
}
std::uint64_t GameHostControls::prepare(AssetId asset, const Json& state, bool activate, bool run) {
    const auto ticket = game_.prepare(load_game_scene(content_, {asset}), {}, state);
    try {
        release_cursor();
    } catch (...) {
        const auto original = std::current_exception();
        // Cancel the new candidate only if it is still the pending one; reentrant
        // mutations may have replaced it with a different ticket.
        if (game_.status().at("prepared_ticket").get<std::uint64_t>() == ticket) {
            try {
                game_.cancel(ticket);
            } catch (...) {
                // Candidate already retired or unmatched; nothing else to do.
            }
        }
        if (auto_ticket_ == ticket)
            auto_ticket_ = 0;
        std::rethrow_exception(original);
    }
    auto_ticket_ = activate ? ticket : 0;
    auto_run_ = run;
    return ticket;
}
void GameHostControls::apply_settings(Json user) {
    const auto candidate = resolve_game_settings(defaults_, user, project_input_);
    auto window_settings = [](const Json& value) {
        auto display = value.at("display");
        display.erase("vsync"); // Present reads VSync directly; it does not resize the SDL window.
        return display;
    };
    const bool display_changed = window_settings(candidate) != window_settings(settings_);
    const bool input_changed = candidate.at("input_map") != settings_.at("input_map");
    const bool audio_changed = candidate.at("audio") != settings_.at("audio");
    try {
        if (display_changed && platform_.settings)
            platform_.settings(candidate);
        if (input_changed)
            game_.input_map(InputMap(candidate.at("input_map")));
        if (audio_changed)
            game_.master_volume(candidate.at("audio").at("master_volume").get<float>());
        storage_.save_settings(user, [&](const Json& value) {
            (void)resolve_game_settings(defaults_, value, project_input_);
        });
    } catch (...) {
        const auto original = std::current_exception();
        try {
            if (display_changed && platform_.settings)
                platform_.settings(settings_);
            if (input_changed)
                game_.input_map(InputMap(settings_.at("input_map")));
            if (audio_changed)
                game_.master_volume(settings_.at("audio").at("master_volume").get<float>());
        } catch (const std::exception& e) {
            std::clog << "Settings rollback failed: " << e.what() << '\n';
        }
        std::rethrow_exception(original);
    }
    user_ = std::move(user);
    settings_ = candidate;
}
// Lifecycle-mutating requests fence pending backend state with a polled
// (token,false) dispatch when cursor_poll is configured, regardless of the
// host cursor_ flag. Returns false to requeue, throws to fail, true (or no
// adapter) to let the caller perform the synchronous release_cursor().
bool GameHostControls::poll_cursor_release(std::uint64_t token) {
    if (!platform_.cursor_poll)
        return true;
    return platform_.cursor_poll(token, false);
}
std::optional<Json> GameHostControls::execute(std::uint64_t token, const Json& command,
                                              const GameSaveSchema* schema,
                                              const std::string& module, RuntimeClock::Time now) {
    const auto operation = command.at("operation").get<std::string>();
    if (operation == "pause") {
        if (!poll_cursor_release(token))
            return std::nullopt;
        release_cursor();
        game_.pause(now);
    } else if (operation == "resume") {
        game_.resume(now);
    } else if (operation == "quit") {
        if (!poll_cursor_release(token))
            return std::nullopt;
        release_cursor();
        quit_ = true;
    } else if (operation == "unload") {
        if (!poll_cursor_release(token))
            return std::nullopt;
        release_cursor();
        game_.unload(now);
        auto_ticket_ = 0;
        rebind_action_.reset();
        rebind_candidate_.reset();
    } else if (operation == "prepare") {
        // Parse command fields and resolve schema prerequisites first so a
        // malformed request fails fast before any physical release dispatch.
        // The schema->validate callback runs only AFTER the polled release
        // ack — otherwise a queue re-entry would repeat user validation work
        // and any schema side-effects.
        const auto asset = command.at("asset").get<AssetId>();
        const auto activate = command.value("activate", true);
        const auto run = command.value("run", true);
        Json restore = Json::object();
        const bool restoring = command.contains("state");
        if (restoring) {
            if (!schema)
                throw std::runtime_error("Restoring game state requires a registered save schema");
        }
        // prepare() unconditionally calls release_cursor(), so every prepare
        // must fence pending backend state with a release poll, including
        // activate=false. load activate=false does NOT prepare and remains
        // immediate below.
        if (!poll_cursor_release(token))
            return std::nullopt;
        if (restoring) {
            schema->validate({asset, command.at("state")});
            restore[module] = command.at("state");
        }
        return Json{{"ticket", prepare(asset, restore, activate, run)}};
    } else if (operation == "activate") {
        game_.activate(command.at("ticket").get<std::uint64_t>(), now, command.value("run", true));
        auto_ticket_ = 0;
        rebind_action_.reset();
        rebind_candidate_.reset();
    } else if (operation == "cancel") {
        game_.cancel(command.at("ticket").get<std::uint64_t>());
        auto_ticket_ = 0;
    } else if (operation == "settings") {
        return settings_;
    } else if (operation == "set_settings") {
        auto user = user_;
        const auto& changes = command.at("values");
        if (!changes.is_object())
            throw std::runtime_error("Settings changes must be an object");
        for (const auto& [group, values] : changes.items()) {
            if (!values.is_object())
                throw std::runtime_error("Settings group must be an object");
            for (const auto& [name, value] : values.items())
                user[group][name] = value;
        }
        apply_settings(std::move(user));
        return settings_;
    } else if (operation == "cursor") {
        const bool capture = command.at("capture").get<bool>();
        if (platform_.cursor_poll) {
            if (platform_.cursor_poll(token, capture)) {
                cursor_ = capture;
                game_.input({{{}, 0, true}});
                return Json::object();
            }
            return std::nullopt;
        }
        if (!platform_.cursor && capture)
            throw std::runtime_error("This runtime host has no cursor capture adapter");
        if (platform_.cursor)
            platform_.cursor(capture);
        cursor_ = capture;
        game_.input({{{}, 0, true}});
    } else if (operation == "ui_navigation") {
        const auto direction = command.at("direction").get<std::string>();
        if (direction != "next" && direction != "previous" && direction != "accept")
            throw std::runtime_error("Unknown UI navigation action");
        if (platform_.navigation_poll) {
            if (platform_.navigation_poll(token, direction))
                return Json::object();
            return std::nullopt;
        }
        if (!platform_.navigation)
            throw std::runtime_error("This host has no UI navigation adapter");
        platform_.navigation(direction);
    } else if (operation == "input_contexts") {
        game_.active().simulation.input().activate_contexts(
            command.at("active").get<std::vector<std::string>>());
    } else if (operation == "rebind_begin") {
        const auto id = command.at("action").get<ActionId>();
        const auto index = command.at("index").get<std::size_t>();
        auto& input = game_.active().simulation.input();
        const auto& actions = input.map().actions();
        const auto action = std::find_if(actions.begin(), actions.end(),
                                         [&](const auto& value) { return value.id == id; });
        if (action == actions.end() || index > action->bindings.size() || index >= 16)
            throw std::runtime_error("Invalid action or binding slot");
        input.begin_rebind();
        rebind_action_ = id;
        rebind_index_ = index;
        rebind_candidate_.reset();
    } else if (operation == "rebind_cancel") {
        game_.active().simulation.input().cancel_rebind();
        rebind_action_.reset();
        rebind_candidate_.reset();
    } else if (operation == "rebind_reset") {
        auto user = user_;
        if (user.contains("input") && user["input"].contains("bindings")) {
            if (command.contains("action"))
                user["input"]["bindings"].erase(command.at("action").get<ActionId>().str());
            else
                user["input"].erase("bindings");
        }
        apply_settings(std::move(user));
        game_.active().simulation.input().cancel_rebind();
        rebind_action_.reset();
        rebind_candidate_.reset();
    } else if (operation == "rebind_commit") {
        if (!rebind_action_ || (!rebind_candidate_ && !command.value("clear", false)))
            throw std::runtime_error("No captured binding to commit");
        const auto& input = game_.active().simulation.input();
        Json bindings;
        for (const auto& action : input.map().source().at("actions"))
            if (action.at("id").get<ActionId>() == *rebind_action_)
                bindings = action.at("bindings");
        if (command.value("clear", false)) {
            if (rebind_index_ >= bindings.size())
                throw std::runtime_error("No existing binding in this slot");
            bindings.erase(bindings.begin() + rebind_index_);
        } else {
            const auto conflicts =
                input.map().binding_conflicts(*rebind_action_, rebind_candidate_->control);
            if (!command.value("allow_conflicts", false))
                for (const auto& conflict : conflicts)
                    if (conflict.same_context && conflict.action != *rebind_action_)
                        throw std::runtime_error("Control is already used in this context");
            Json binding = rebind_index_ < bindings.size() ? bindings[rebind_index_]
                                                           : Json{{"x", 1}, {"y", 0}};
            binding["control"] = rebind_candidate_->control;
            binding.erase("radial");
            binding.erase("deadzone");
            binding.erase("threshold");
            binding.erase("direction");
            const auto& control = input_control(rebind_candidate_->control);
            const auto sign = rebind_candidate_->value < 0 ? -1 : 1;
            for (const auto& action : input.map().actions())
                if (action.id == *rebind_action_) {
                    if (action.kind == ActionKind::Digital && !control.digital)
                        binding["direction"] = sign;
                    else if (!control.digital) {
                        binding["x"] = binding.value("x", 1.0) * sign;
                        binding["y"] = binding.value("y", 0.0) * sign;
                    }
                }
            if (rebind_index_ < bindings.size())
                bindings[rebind_index_] = std::move(binding);
            else
                bindings.push_back(std::move(binding));
        }
        (void)input.map().with_bindings(*rebind_action_, bindings);
        auto user = user_;
        user["input"]["bindings"][rebind_action_->str()] = std::move(bindings);
        apply_settings(std::move(user));
        rebind_action_.reset();
        rebind_candidate_.reset();
    } else if (operation == "slots") {
        Json slots = Json::array();
        for (const auto& name : storage_.slots()) {
            Json item{{"name", name}, {"valid", false}};
            try {
                if (!schema)
                    throw std::runtime_error("No save schema is registered");
                item["scene"] = storage_.load(name, *schema).scene;
                item["schema_version"] = schema->version;
                item["valid"] = true;
            } catch (const std::exception& e) {
                item["error"] = std::string(e.what()).substr(0, 1024);
            }
            slots.push_back(std::move(item));
        }
        return slots;
    } else if (operation == "erase") {
        storage_.erase(command.at("slot").get<std::string>());
    } else if (operation == "save" || operation == "load") {
        // Schema prerequisite and slot field are required for both save and
        // load. Reject before any release dispatch so a malformed request
        // never causes an extra physical effect.
        if (!schema)
            throw std::runtime_error("Game module has not registered its save schema");
        const auto slot = command.at("slot").get<std::string>();
        if (operation == "save") {
            storage_.save(slot, {command.at("scene").get<AssetId>(), command.at("data")}, *schema);
            return Json{{"saved", slot}};
        }
        // Load: parse activate + run first so malformed run fails before
        // any physical release. Single storage_.load + migration; result
        // branches on activate. activate=true fences pending backend with
        // a release poll before reading storage.
        const bool activate = command.value("activate", true);
        const auto run = command.value("run", true);
        if (activate && !poll_cursor_release(token))
            return std::nullopt;
        const auto save = storage_.load(slot, *schema);
        if (activate)
            return Json{{"ticket", prepare(save.scene, {{module, save.data}}, true, run)}};
        return Json{{"scene", save.scene}, {"data", save.data}};
    } else {
        throw std::runtime_error("Unsupported game operation");
    }
    return Json::object();
}
void GameHostControls::publish() {
    auto state = game_.status();
    state["settings"] = settings_;
    state["input_device"] = platform_.input_device ? platform_.input_device() : "KeyboardMouse";
    state["cursor_captured"] = cursor_;
    state["error"] = error_.empty() ? state.at("error") : Json(error_);
    state["rebind"] = {{"listening", false}};
    if (state.at("state") == "running" || state.at("state") == "paused") {
        state["scene"] = game_.active().scene.asset_id();
        auto& input = game_.active().simulation.input();
        state["rebind"]["listening"] = input.rebinding();
        if (auto captured = input.take_rebind())
            rebind_candidate_ = std::move(captured);
        if (rebind_action_)
            state["rebind"]["action"] = *rebind_action_;
        if (rebind_candidate_) {
            state["rebind"]["control"] = rebind_candidate_->control;
            state["rebind"]["value"] = rebind_candidate_->value;
            Json conflicts = Json::array();
            if (rebind_action_)
                for (const auto& conflict :
                     input.map().binding_conflicts(*rebind_action_, rebind_candidate_->control))
                    conflicts.push_back({{"action", conflict.action},
                                         {"context", conflict.context},
                                         {"same_context", conflict.same_context}});
            state["rebind"]["conflicts"] = std::move(conflicts);
        }
    }
    queue_->status(std::move(state));
}
void GameHostControls::pump(RuntimeClock::Time now) {
    queue_->poll([&](std::uint64_t token, const Json& command, const GameSaveSchema* schema,
                     const std::string& module) -> std::optional<Json> {
        try {
            auto result = execute(token, command, schema, module, now);
            error_.clear();
            return result;
        } catch (const std::exception& e) {
            error_ = std::string(e.what()).substr(0, 1024);
            std::clog << "Game operation " << command.value("operation", "unknown")
                      << " failed: " << error_ << '\n';
            throw;
        }
    });
    const auto pending = game_.status().at("prepared_ticket").get<std::uint64_t>();
    if (pending && !quit_) {
        try {
            const auto progress = game_.poll_preparation(pending);
            if (progress.ready && auto_ticket_ == pending) {
                game_.activate(pending, now, auto_run_);
                auto_ticket_ = 0;
                rebind_action_.reset();
                rebind_candidate_.reset();
            }
        } catch (const std::exception& e) {
            error_ = std::string(e.what()).substr(0, 1024);
            auto_ticket_ = 0;
            std::clog << "Scene transition failed: " << error_ << '\n';
        }
    }
    publish();
}
} // namespace forge
