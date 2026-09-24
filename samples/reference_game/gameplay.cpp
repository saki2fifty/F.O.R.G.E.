// Project gameplay. Uses only the installed exact SDK; no host, SDL or Jolt access.
#include "ids.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <flecs.h>
#include <forge/identity.hpp>
#include <forge/native_sdk_identity.h>
#include <forge/sdk_client.hpp>
#include <forge/transform_components.hpp>
#include <forge/ui_components.hpp>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace reference {
using Json = nlohmann::json;
struct Pending {
    uint64_t token;
    std::string operation;
};
struct State {
    std::string page = "main", message, prompt, scene, last_host_error;
    double yaw = 0, pitch = 0, elapsed = 0, eye_height = 1.5;
    int interactions = 0;
    bool initialized = false, have_save = false;
    Json settings = Json::object();
    std::vector<Pending> pending;
    std::vector<std::string> ui_entities;
};
flecs::entity find(flecs::world& world, const char* id) {
    flecs::entity result;
    world.each([&](flecs::entity e, const forge::StableId& stable) {
        if (stable.value == id)
            result = e;
    });
    return result;
}
State& state(const ForgeSdkWorldV1* host) { return flecs::world(host->world).get_mut<State>(); }
template <class F> Json copied(F&& get) {
    const auto size = get(nullptr, 0);
    if (!size)
        return Json();
    if (size > 1024 * 1024)
        throw std::runtime_error("Oversized copied service value");
    std::vector<char> text(size);
    if (get(text.data(), size) != size)
        throw std::runtime_error("Service value changed while reading");
    return Json::parse(text.data());
}
Json query(const ForgeSdkWorldV1* h) {
    return copied([&](char* b, uint32_t n) { return h->game_query(h->context, b, n); });
}
void request(const ForgeSdkWorldV1* h, Json value) {
    auto& s = state(h);
    const auto token = h->game_request(h->context, value.dump().c_str());
    if (!token)
        throw std::runtime_error("Game request was rejected: " +
                                 value.at("operation").get<std::string>());
    s.pending.push_back({token, value.at("operation")});
}
void publish(const ForgeSdkWorldV1* h, const char* name, const Json& value) {
    if (!forge::sdk::Client(h).available(forge::sdk::Capability::Ui))
        return;
    for (const auto& entity : state(h).ui_entities)
        if (!h->ui_publish_json(h->context, entity.c_str(), name, value.dump().c_str()))
            throw std::runtime_error(std::string("Could not publish reference UI: ") + name);
}
void model(const ForgeSdkWorldV1* h) {
    const auto& s = state(h);
    publish(h, "page", s.page);
    publish(h, "message", s.message);
    publish(h, "prompt", s.prompt);
    publish(h, "interactions", s.interactions);
    publish(h, "have_save", s.have_save);
}
ForgeSdkActionV1 action(const ForgeSdkWorldV1* h, const char* id, bool control = false) {
    ForgeSdkActionV1 out{};
    if (control)
        forge::sdk::Client(h).control(id, out);
    else
        forge::sdk::Client(h).action(id, out);
    return out;
}
void mode(const ForgeSdkWorldV1* h, bool play) {
    request(h, {{"operation", "input_contexts"},
                {"active",
                 play ? Json::array({"Gameplay", "Global"}) : Json::array({"Menu", "Global"})}});
    request(h, {{"operation", play ? "resume" : "pause"}});
    request(h, {{"operation", "cursor"}, {"capture", play}});
}
void camera(flecs::world& world, State& s) {
    const auto player = find(world, player_id), view = find(world, camera_id);
    if (!player || !view)
        return;
    const auto p = player.get<forge::LocalTranslation>();
    view.set<forge::LocalTranslation>({p.x, p.y + s.eye_height, p.z});
    const double sy = std::sin(s.yaw * .5), cy = std::cos(s.yaw * .5);
    const double sp = std::sin(s.pitch * .5), cp = std::cos(s.pitch * .5);
    view.set<forge::LocalRotation>(
        {float(cy * sp), float(sy * cp), float(-sy * sp), float(cy * cp)});
}
void fixed(const ForgeSdkWorldV1* h, float dt) {
    auto& s = state(h);
    flecs::world world(h->world);
    const auto player = find(world, player_id);
    if (!player)
        return;
    const auto m = action(h, move), mouse = action(h, mouse_look), stick = action(h, stick_look);
    const auto settings = s.settings.value("input", Json::object());
    const auto ms = settings.value("mouse_sensitivity", 1.0) * .002;
    const auto gs = settings.value("gamepad_sensitivity", 1.0) * 2.0;
    s.yaw += mouse.x * ms + stick.x * gs * dt;
    s.pitch += mouse.y * ms * (settings.value("invert_mouse_y", false) ? -1 : 1) +
               stick.y * gs * dt * (settings.value("invert_gamepad_y", false) ? -1 : 1);
    s.yaw = std::remainder(s.yaw, 6.283185307179586);
    s.pitch = std::clamp(s.pitch, -1.5, 1.5);
    const double speed = action(h, sprint).held ? 7 : 4;
    const double velocity[] = {(m.x * std::cos(s.yaw) + m.y * std::sin(s.yaw)) * speed, 0,
                               (m.y * std::cos(s.yaw) - m.x * std::sin(s.yaw)) * speed};
    forge::sdk::Client api(h);
    if (!api.character_command(s.scene.c_str(), player_id, 0, velocity))
        throw std::runtime_error("Reference character movement rejected");
    if (action(h, jump).pressed)
        api.character_command(s.scene.c_str(), player_id, 1, nullptr, nullptr, 5);
    api.character_command(s.scene.c_str(), player_id, 2, nullptr, nullptr, 0,
                          action(h, crouch).held);
    camera(world, s);
    const auto p = player.get<forge::LocalTranslation>();
    const double origin[] = {p.x, p.y + s.eye_height, p.z};
    const double ray[] = {std::sin(s.yaw) * std::cos(s.pitch) * 3, -std::sin(s.pitch) * 3,
                          std::cos(s.yaw) * std::cos(s.pitch) * 3};
    ForgeSdkPhysicsHitV1 hit{};
    const bool found = api.raycast_filtered(origin, ray, 1, false, hit) == 1;
    const bool target = found && std::string(hit.entity) == switch_id;
    const bool door = found && std::string(hit.entity) == door_id;
    s.prompt = target ? "Interact to activate the beacon"
               : door ? "Interact to return to the main menu"
                      : "";
    if (action(h, interact).pressed) {
        if (target) {
            ++s.interactions;
            s.message = "Beacon activated";
        }
        if (door)
            request(h, {{"operation", "prepare"}, {"asset", menu_scene}, {"run", false}});
    }
    s.elapsed += dt;
    const double platform[] = {4, .15, -3 + std::sin(s.elapsed) * 2};
    const float rotation[] = {0, 0, 0, 1};
    h->physics_move(h->context, s.scene.c_str(), platform_id, platform, rotation, 1, 0);
}
void validate(const Json& value) {
    if (value.at("scene") != level_scene)
        throw std::runtime_error("Save references an unavailable reference level");
    const auto& d = value.at("data");
    if (!d.is_object() || d.size() != 4 || !d.at("position").is_array() ||
        d.at("position").size() != 3 || !d.at("interactions").is_number_integer() ||
        d.at("interactions") < 0 || d.at("interactions") > 1000000)
        throw std::runtime_error("Invalid reference save state");
    for (const auto& n : d.at("position"))
        if (!n.is_number() || !std::isfinite(n.get<double>()) || std::abs(n.get<double>()) > 10000)
            throw std::runtime_error("Invalid saved player position");
    for (auto name : {"yaw", "pitch"})
        if (!d.at(name).is_number() || !std::isfinite(d.at(name).get<double>()) ||
            std::abs(d.at(name).get<double>()) > 6.284)
            throw std::runtime_error("Invalid saved camera angle");
}
template <class F> int guarded(char* error, uint32_t capacity, F&& f) {
    try {
        f();
        return 1;
    } catch (const std::exception& e) {
        if (error && capacity)
            std::snprintf(error, capacity, "%s", e.what());
        return 0;
    }
}
int32_t FORGE_SDK_CALL save_validate(void*, const char* value, char* error, uint32_t capacity) {
    return guarded(error, capacity, [&] { validate(Json::parse(value)); });
}
int32_t FORGE_SDK_CALL schema(const ForgeSdkWorldV1* h, char* error, uint32_t capacity) {
    return guarded(error, capacity, [&] {
        flecs::world w(h->world);
        w.component<State>("reference.session");
        w.component<forge::StableId>("forge.stable_id");
        w.component<forge::UiDocument>("forge.ui_document");
        w.component<forge::LocalTranslation>("forge.local_translation");
        w.component<forge::LocalRotation>("forge.local_rotation");
    });
}
int32_t FORGE_SDK_CALL start(const ForgeSdkWorldV1* h, char* error, uint32_t capacity) {
    return guarded(error, capacity, [&] {
        flecs::world w(h->world);
        w.set<State>({});
        if (forge::sdk::Client(h).available(forge::sdk::Capability::Game)) {
            ForgeSdkSaveSchemaV1 save{sizeof(save), 1, nullptr, save_validate, nullptr, nullptr, 0};
            if (!h->game_save_schema(h->context, &save))
                throw std::runtime_error("Save schema registration failed");
        }
        if (forge::sdk::Client(h).available(forge::sdk::Capability::Ui) &&
            !h->ui_allow_value_action(h->context, "Reference"))
            throw std::runtime_error("UI registration failed");
        w.system()
            .kind(h->fixed_phase)
            .run([h](flecs::iter& it) { fixed(h, it.delta_time()); })
            .add(h->fixed_tag);
        w.system()
            .kind(h->post_physics_phase)
            .run([h](flecs::iter& it) {
                auto& s = state(h);
                ForgeSdkCharacterV1 character{};
                if (forge::sdk::Client(h).character(s.scene.c_str(), player_id, character)) {
                    // Follow the accepted shape, not the requested key: blocked standing stays low.
                    const double target = character.crouched ? .75 : 1.5;
                    s.eye_height +=
                        (target - s.eye_height) * (1 - std::exp(-12.0 * it.delta_time()));
                }
                flecs::world w(h->world);
                camera(w, s);
            })
            .add(h->fixed_tag);
    });
}
int32_t FORGE_SDK_CALL ready(const ForgeSdkWorldV1* h, char* error, uint32_t capacity) {
    return guarded(error, capacity, [&] {
        flecs::world w(h->world);
        auto& s = state(h);
        w.each([&](const forge::UiDocument&, const forge::StableId& id) {
            s.ui_entities.push_back(id.value);
        });
        const bool play = bool(find(w, player_id));
        s.scene = play ? level_scene : menu_scene;
        s.page = play ? "play" : "main";
        camera(w, s);
        model(h);
        publish(h, "binding", "Choose Rebind Jump");
        publish(h, "conflicts", "");
        publish(h, "settings_text", "");
    });
}
int32_t FORGE_SDK_CALL restore(const ForgeSdkWorldV1* h, const char* text, char* error,
                               uint32_t capacity) {
    return guarded(error, capacity, [&] {
        const auto d = Json::parse(text);
        validate({{"scene", level_scene}, {"data", d}});
        flecs::world w(h->world);
        auto p = find(w, player_id);
        if (!p)
            throw std::runtime_error("Save destination has no player");
        auto& s = state(h);
        s.yaw = d.at("yaw");
        s.pitch = d.at("pitch");
        s.interactions = d.at("interactions");
        p.set<forge::LocalTranslation>(
            {d.at("position")[0], d.at("position")[1], d.at("position")[2]});
        s.message = "Save loaded";
        camera(w, s);
        model(h);
    });
}
void command(const ForgeSdkWorldV1* h, const std::string& value) {
    auto& s = state(h);
    if (value == "start")
        request(h, {{"operation", "prepare"}, {"asset", level_scene}});
    else if (value == "main")
        request(h, {{"operation", "prepare"}, {"asset", menu_scene}, {"run", false}});
    else if (value == "quit")
        request(h, {{"operation", "quit"}});
    else if (value == "resume") {
        s.page = "play";
        mode(h, true);
    } else if (value == "options")
        s.page = "options";
    else if (value == "back")
        s.page = s.scene == level_scene ? "pause" : "main";
    else if (value == "load")
        request(h, {{"operation", "load"}, {"slot", "one"}});
    else if (value == "save") {
        flecs::world w(h->world);
        const auto p = find(w, player_id).get<forge::LocalTranslation>();
        request(h, {{"operation", "save"},
                    {"slot", "one"},
                    {"scene", s.scene},
                    {"data",
                     {{"position", {p.x, p.y, p.z}},
                      {"yaw", s.yaw},
                      {"pitch", s.pitch},
                      {"interactions", s.interactions}}}});
    } else if (value == "rebind_jump")
        request(h, {{"operation", "rebind_begin"}, {"action", jump}, {"index", 0}});
    else if (value == "rebind_apply")
        request(h, {{"operation", "rebind_commit"}});
    else if (value == "rebind_share")
        request(h, {{"operation", "rebind_commit"}, {"allow_conflicts", true}});
    else if (value == "rebind_clear") {
        request(h, {{"operation", "rebind_begin"}, {"action", jump}, {"index", 0}});
        request(h, {{"operation", "rebind_commit"}, {"clear", true}});
    } else if (value == "rebind_cancel")
        request(h, {{"operation", "rebind_cancel"}});
    else if (value == "rebind_reset")
        request(h, {{"operation", "rebind_reset"}});
    else if (value == "volume_down" || value == "volume_up") {
        const double volume = s.settings.at("audio").at("master_volume");
        request(h, {{"operation", "set_settings"},
                    {"values",
                     {{"audio",
                       {{"master_volume",
                         std::clamp(volume + (value == "volume_up" ? .1 : -.1), 0.0, 1.0)}}}}}});
    } else if (value == "mouse_down" || value == "mouse_up" || value == "pad_down" ||
               value == "pad_up") {
        const auto key = value.starts_with("mouse") ? "mouse_sensitivity" : "gamepad_sensitivity";
        const double sensitivity = s.settings.at("input").at(key);
        request(h, {{"operation", "set_settings"},
                    {"values",
                     {{"input",
                       {{key, std::clamp(sensitivity + (value.ends_with("up") ? .25 : -.25), .25,
                                         4.0)}}}}}});
    } else if (value == "invert_mouse" || value == "invert_pad") {
        const auto key = value == "invert_mouse" ? "invert_mouse_y" : "invert_gamepad_y";
        request(h, {{"operation", "set_settings"},
                    {"values", {{"input", {{key, !s.settings.at("input").at(key).get<bool>()}}}}}});
    } else if (value == "vsync") {
        request(h,
                {{"operation", "set_settings"},
                 {"values",
                  {{"display", {{"vsync", !s.settings.at("display").at("vsync").get<bool>()}}}}}});
    } else if (value.starts_with("setting:")) {
        const auto fields = Json::parse(value.substr(8));
        request(h, {{"operation", "set_settings"}, {"values", fields}});
    }
}
int32_t FORGE_SDK_CALL controls(const ForgeSdkWorldV1* h, char* error, uint32_t capacity) {
    return guarded(error, capacity, [&] {
        auto& s = state(h);
        const auto q = query(h);
        if (q.is_null())
            return; // Editor inspection/headless host may omit session services.
        s.settings = q.at("settings");
        const auto host_error = q.value("error", std::string{});
        if (!host_error.empty() && host_error != s.last_host_error)
            s.message = host_error;
        s.last_host_error = host_error;
        for (auto it = s.pending.begin(); it != s.pending.end();) {
            const auto receipt = copied(
                [&](char* b, uint32_t n) { return h->game_inspect(h->context, it->token, b, n); });
            if (receipt.is_null() || receipt.at("state") == "queued") {
                ++it;
                continue;
            }
            if (receipt.at("state") == "failed")
                s.message = receipt.at("error");
            else if (it->operation == "save") {
                s.message = "Game saved";
                s.have_save = true;
            } else if (it->operation == "slots") {
                for (const auto& slot : receipt.at("value"))
                    if (slot.at("name") == "one") {
                        s.have_save = slot.at("valid");
                        if (!s.have_save)
                            s.message = slot.at("error");
                    }
            }
            h->game_release(h->context, it->token);
            it = s.pending.erase(it);
        }
        if (!s.initialized) {
            s.initialized = true;
            mode(h, s.page == "play");
            request(h, {{"operation", "slots"}});
        } else if (s.page == "play" && !q.at("cursor_captured").get<bool>()) {
            s.page = "pause";
            mode(h, false);
        }
        if (action(h, pause, true).pressed) {
            if (s.page == "play") {
                s.page = "pause";
                mode(h, false);
            } else if (s.page == "pause")
                command(h, "resume");
        }
        if (action(h, back, true).pressed)
            command(h, s.page == "pause" ? "resume" : "back");
        for (const auto& [id, direction] :
             {std::pair{next, "next"}, {previous, "previous"}, {accept, "accept"}})
            if (action(h, id, true).pressed)
                request(h, {{"operation", "ui_navigation"}, {"direction", direction}});
        for (unsigned n = 0; n < 32; ++n) {
            const auto event = copied([&](char* b, uint32_t z) {
                return h->ui_poll_event(h->context, "Reference", b, z);
            });
            if (event.is_null())
                break;
            command(h, event.at("value"));
        }
        publish(h, "settings_text",
                "Volume " +
                    std::to_string(s.settings.at("audio").at("master_volume").get<double>())
                        .substr(0, 4) +
                    " | Mouse " +
                    std::to_string(s.settings.at("input").at("mouse_sensitivity").get<double>())
                        .substr(0, 4) +
                    " | Gamepad " +
                    std::to_string(s.settings.at("input").at("gamepad_sensitivity").get<double>())
                        .substr(0, 4) +
                    " | Invert Y: mouse " +
                    (s.settings.at("input").at("invert_mouse_y").get<bool>() ? "on" : "off") +
                    ", gamepad " +
                    (s.settings.at("input").at("invert_gamepad_y").get<bool>() ? "on" : "off") +
                    " | VSync " +
                    (s.settings.at("display").at("vsync").get<bool>() ? "on" : "off"));
        const auto& binding = q.at("rebind");
        publish(h, "binding",
                binding.value("listening", false)
                    ? "Press a key or controller button. Escape cancels."
                    : binding.value("control", std::string("Choose Rebind Jump")));
        publish(h, "conflicts",
                binding.contains("conflicts") && !binding.at("conflicts").empty()
                    ? "This control is also bound. Choose Apply and share control, or cancel."
                    : "");
        model(h);
    });
}
const char* dependencies[] = {"forge.input", "forge.physics", "forge.game", "forge.ui"};
const ForgeNativeSdkV1 api = {sizeof(api),
                              FORGE_NATIVE_SDK_ABI,
                              FORGE_NATIVE_SDK_FINGERPRINT,
                              "project.reference",
                              "1",
                              dependencies,
                              4,
                              FORGE_SDK_RUNTIME | FORGE_SDK_AUTHORING | FORGE_SDK_VALIDATION,
                              FORGE_SDK_RUNTIME,
                              FORGE_SDK_PHYSICS,
                              FORGE_SDK_GAME | FORGE_SDK_UI | FORGE_SDK_PHYSICS | FORGE_SDK_AUDIO |
                                  FORGE_SDK_NAVIGATION | FORGE_SDK_DIAGNOSTICS,
                              &ecs_init,
                              &ecs_os_api,
                              schema,
                              start,
                              nullptr,
                              controls,
                              restore,
                              ready};
} // namespace reference
extern "C" FORGE_SDK_EXPORT const ForgeNativeSdkV1* FORGE_SDK_CALL forge_native_sdk_v1() {
    return &reference::api;
}
