// Original Gallery behavior: show raw readings; no orientation/fusion estimate.
#include <algorithm>
#include <cstdio>
#include <flecs.h>
#include <forge/sdk_client.hpp>
#include <forge/transform_components.hpp>
#include <nlohmann/json.hpp>
#include <vector>
namespace {
using Json = nlohmann::json;
struct MotionRequest {
    uint64_t token = 0, failed_revision = 0;
};
template <class Copy> Json copied(Copy copy) {
    const auto size = copy(nullptr, 0);
    if (!size || size > 1024 * 1024)
        return Json::object();
    std::vector<char> text(size);
    if (copy(text.data(), size) != size)
        return Json::object();
    return Json::parse(text.data());
}
Json observation(const ForgeSdkWorldV1* host) {
    const forge::sdk::Client client(host);
    if (!client.callable(forge::sdk::Capability::Game))
        return Json::object();
    return copied(
        [&](char* text, uint32_t size) { return host->game_query(host->context, text, size); });
}
} // namespace
// Registering this example opts into enabling supported sensors on Player 1.
// Requests use the unscaled control callback, so their receipts progress paused.
void forge_control_GamepadMotion(const ForgeSdkWorldV1* host) {
    flecs::world world(host->world);
    auto pending = world.get<MotionRequest>();
    if (pending.token) {
        const auto receipt = copied([&](char* text, uint32_t size) {
            return host->game_inspect(host->context, pending.token, text, size);
        });
        if (receipt.value("state", "queued") == "queued")
            return;
        if (!host->game_release(host->context, pending.token))
            throw std::runtime_error("Motion request release failed");
        pending.token = 0;
        // Retain the attempted revision after failure; do not flood requests.
        if (receipt.at("state") == "succeeded")
            pending.failed_revision = 0;
        world.set(pending);
    }
    const auto query = observation(host);
    if (!query.value("gamepad_sensor_host", false) || !query.contains("gamepad_motion"))
        return;
    const auto& motion = query.at("gamepad_motion");
    const auto revision = motion.at("revision").get<uint64_t>();
    if (!motion.at("available").get<bool>() || revision == pending.failed_revision)
        return;
    for (const auto& device : motion.at("devices")) {
        if (device.at("slot") != 1 || !device.at("connected").get<bool>())
            continue;
        for (const auto* kind : {"accelerometer", "gyroscope"}) {
            const auto& sensor = device.at(kind);
            if (!sensor.at("supported").get<bool>() || sensor.at("enabled").get<bool>())
                continue;
            const auto command = Json{
                {"operation", "gamepad_sensor"},
                {"revision", revision},
                {"device", device.at("device")},
                {"sensor", kind},
                {"enabled", true}}.dump();
            pending.failed_revision = revision;
            pending.token = host->game_request(host->context, command.c_str());
            if (!pending.token)
                throw std::runtime_error("Motion request rejected");
            world.set(pending);
            return;
        }
    }
}
void forge_register_system_GamepadMotion(const ForgeSdkWorldV1* host) {
    flecs::world world(host->world);
    world.component<forge::LocalTranslation>("forge.local_translation");
    world.set<MotionRequest>({});
    world.system("gameplay.GalleryGamepadMotion")
        .kind(host->fixed_phase)
        .write<forge::LocalTranslation>()
        .run([host](flecs::iter& it) {
            const auto query = observation(host);
            const Json* device = nullptr;
            if (query.contains("gamepad_motion"))
                for (const auto& row : query.at("gamepad_motion").at("devices"))
                    if (row.at("slot") == 1 && row.at("connected").get<bool>()) {
                        device = &row;
                        break;
                    }
            const forge::sdk::Client client(host);
            it.world().each([&](flecs::entity entity, forge::LocalTranslation& position) {
                ForgeSdkEntityRefV1 reference{};
                if (client.entity_reference(entity, reference) != 1 ||
                    std::string_view(reference.scene_uuid) !=
                        "16800000-1680-4168-8168-168000000000")
                    return;
                const bool accel = std::string_view(reference.entity_uuid) ==
                                   "16800000-1680-4168-8168-000000000002";
                if (!accel && std::string_view(reference.entity_uuid) !=
                                  "16800000-1680-4168-8168-000000000003")
                    return;
                const double base = accel ? -3 : 3, scale = accel ? .2 : 1;
                position = {base, 0, 0};
                if (!device)
                    return;
                const auto& sensor = device->at(accel ? "accelerometer" : "gyroscope");
                if (!sensor.at("measured").get<bool>())
                    return;
                const auto& value = sensor.at("value");
                position.x += std::clamp(value[0].get<double>() * scale, -2., 2.);
                position.y = std::clamp(value[1].get<double>() * scale, -3., 3.);
            });
        })
        .add(host->fixed_tag);
}
