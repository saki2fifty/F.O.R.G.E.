// Original exact-SDK example: control input and real-time timers survive simulation holds.
#include <cstdio>
#include <flecs.h>
#include <forge/native_sdk_identity.h>
#include <forge/sdk_client.hpp>
#include <nlohmann/json.hpp>
#include <vector>
namespace {
struct RateProbe {
    uint64_t frames = 0, ticks = 0;
    float last_dt = 0;
    double observed_rate = 1;
    double unscaled_time = 0, unscaled_delta = 0, timer_seconds = 0;
    uint64_t unscaled_sequence = 0, timer_updates = 0;
};
struct PendingRate {
    uint64_t token = 0;
};
constexpr const char* action_ids[]{
    "12345678-1234-4234-8234-123456789a01", "12345678-1234-4234-8234-123456789a02",
    "12345678-1234-4234-8234-123456789a03", "12345678-1234-4234-8234-123456789a04"};
constexpr double rates[]{0, .25, 1, 4};
int32_t FORGE_SDK_CALL schemas(const ForgeSdkWorldV1* h, char* error, uint32_t capacity) {
    try {
        flecs::world world(h->world);
        const auto type = world.component<RateProbe>("gameplay.RateProbe")
                              .member<uint64_t>("frames")
                              .member<uint64_t>("ticks")
                              .member<float>("last_dt")
                              .member<double>("observed_rate")
                              .member<double>("unscaled_time")
                              .member<double>("unscaled_delta")
                              .member<double>("timer_seconds")
                              .member<uint64_t>("unscaled_sequence")
                              .member<uint64_t>("timer_updates");
        world.component<PendingRate>();
        return h->authoring_type(
            h->context, type, "project.rate_probe", 2,
            R"({"frames":0,"ticks":0,"last_dt":0,"observed_rate":1,"unscaled_time":0,"unscaled_delta":0,"timer_seconds":0,"unscaled_sequence":0,"timer_updates":0})",
            "Examples", error, capacity);
    } catch (const std::exception& e) {
        if (capacity)
            std::snprintf(error, capacity, "%s", e.what());
        return 0;
    } catch (...) {
        if (capacity)
            std::snprintf(error, capacity, "%s", "Rate schema registration failed");
        return 0;
    }
}
int32_t FORGE_SDK_CALL start(const ForgeSdkWorldV1* h, char* error, uint32_t capacity) {
    try {
        flecs::world world(h->world);
        world.set<PendingRate>({}); // One state per world; no process-global tokens.
        world.system<RateProbe>("project.RateTick")
            .kind(h->fixed_phase)
            .each([](flecs::iter& it, size_t, RateProbe& probe) {
                ++probe.ticks;
                probe.last_dt = it.delta_time();
            })
            .add(h->fixed_tag);
        return 1;
    } catch (const std::exception& e) {
        if (capacity)
            std::snprintf(error, capacity, "%s", e.what());
        return 0;
    } catch (...) {
        if (capacity)
            std::snprintf(error, capacity, "%s", "Rate system registration failed");
        return 0;
    }
}
int32_t FORGE_SDK_CALL controls(const ForgeSdkWorldV1* h, char* error, uint32_t capacity) {
    try {
        forge::sdk::Client client(h);
        if (!client.callable(forge::sdk::Capability::ControlInput) ||
            client.callable(forge::sdk::Capability::Input))
            throw std::runtime_error("Rate example requires unscaled control input");
        flecs::world world(h->world);
        auto pending = world.get<PendingRate>();
        if (pending.token) {
            const auto needed = h->game_inspect(h->context, pending.token, nullptr, 0);
            if (!needed || needed > 65536)
                throw std::runtime_error("Rate receipt unavailable");
            std::vector<char> data(needed);
            if (h->game_inspect(h->context, pending.token, data.data(), needed) != needed)
                throw std::runtime_error("Rate receipt changed");
            const auto result = nlohmann::json::parse(data.data());
            if (result.at("state") == "failed")
                throw std::runtime_error(result.at("error").get<std::string>());
            if (result.at("state") == "succeeded") {
                if (!h->game_release(h->context, pending.token))
                    throw std::runtime_error("Rate receipt release failed");
                pending.token = 0;
            }
        }
        if (!pending.token) {
            for (unsigned i = 0; i != 4; ++i) {
                ForgeSdkActionV1 action{};
                if (client.control(action_ids[i], action) && action.pressed) {
                    const auto command =
                        nlohmann::json{{"operation", "time_scale"}, {"scale", rates[i]}}.dump();
                    pending.token = h->game_request(h->context, command.c_str());
                    if (!pending.token)
                        throw std::runtime_error("Rate request rejected");
                    break;
                }
            }
        }
        world.set(pending);
        const auto needed = h->game_query(h->context, nullptr, 0);
        if (!needed || needed > 65536)
            throw std::runtime_error("Rate status unavailable");
        std::vector<char> status(needed);
        if (h->game_query(h->context, status.data(), needed) != needed)
            throw std::runtime_error("Rate status changed");
        const auto query = nlohmann::json::parse(status.data());
        const auto& clock = query.at("clock");
        const auto rate = clock.at("time_scale").get<double>();
        const auto seconds = clock.at("unscaled_time").get<double>();
        const auto delta = clock.at("unscaled_delta").get<double>();
        const auto sequence = clock.at("unscaled_sequence").get<uint64_t>();
        world.each([&](RateProbe& probe) {
            // Several fixed ticks/queries can share one host observation. Use the
            // sequence to consume it once and elapsed time to bridge skipped frames.
            if (probe.frames && sequence != probe.unscaled_sequence) {
                probe.timer_seconds += seconds - probe.unscaled_time;
                ++probe.timer_updates;
            }
            ++probe.frames;
            probe.observed_rate = rate;
            probe.unscaled_time = seconds;
            probe.unscaled_delta = delta;
            probe.unscaled_sequence = sequence;
        });
        return 1;
    } catch (const std::exception& e) {
        if (capacity)
            std::snprintf(error, capacity, "%s", e.what());
        return 0;
    } catch (...) {
        if (capacity)
            std::snprintf(error, capacity, "%s", "Rate control callback failed");
        return 0;
    }
}
const char* dependencies[]{"forge.input", "forge.game", "forge.transforms"};
const ForgeNativeSdkV1 api{sizeof(ForgeNativeSdkV1),
                           FORGE_NATIVE_SDK_ABI,
                           FORGE_NATIVE_SDK_FINGERPRINT,
                           "project.time_scale_probe",
                           "1",
                           dependencies,
                           3,
                           FORGE_SDK_RUNTIME | FORGE_SDK_VALIDATION,
                           FORGE_SDK_RUNTIME,
                           FORGE_SDK_GAME,
                           FORGE_SDK_GAME,
                           &ecs_init,
                           &ecs_os_api,
                           schemas,
                           start,
                           nullptr,
                           controls};
} // namespace
extern "C" FORGE_SDK_EXPORT const ForgeNativeSdkV1* FORGE_SDK_CALL forge_native_sdk_v1() {
    return &api;
}
