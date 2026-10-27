// Original public SDK example: scoped controller feedback from unscaled controls.
#include <cstdio>
#include <flecs.h>
#include <forge/native_sdk_identity.h>
#include <forge/sdk_client.hpp>
#include <nlohmann/json.hpp>
#include <vector>
namespace {
using Json = nlohmann::json;
struct FeedbackProbe {
    uint64_t requested = 0, completed = 0, failed = 0;
};
struct PendingFeedback {
    uint64_t token = 0;
};
template <class Copy> Json read_json(Copy copy) {
    const auto size = copy(nullptr, 0);
    if (!size || size > 65536)
        throw std::runtime_error("Feedback receipt unavailable or too large");
    std::vector<char> data(size);
    if (copy(data.data(), size) != size)
        throw std::runtime_error("Feedback receipt changed during copying");
    return Json::parse(data.data());
}
int32_t FORGE_SDK_CALL schemas(const ForgeSdkWorldV1* h, char* error, uint32_t capacity) {
    try {
        flecs::world world(h->world);
        const auto type = world.component<FeedbackProbe>("gameplay.FeedbackProbe")
                              .member<uint64_t>("requested")
                              .member<uint64_t>("completed")
                              .member<uint64_t>("failed");
        world.component<PendingFeedback>();
        return h->authoring_type(h->context, type, "project.feedback_probe", 1,
                                 R"({"requested":0,"completed":0,"failed":0})", "Examples", error,
                                 capacity);
    } catch (const std::exception& e) {
        if (capacity)
            std::snprintf(error, capacity, "%s", e.what());
        return 0;
    } catch (...) {
        return 0;
    }
}
int32_t FORGE_SDK_CALL start(const ForgeSdkWorldV1* h, char* error, uint32_t capacity) {
    try {
        flecs::world(h->world).set<PendingFeedback>({});
        return 1;
    } catch (const std::exception& e) {
        if (capacity)
            std::snprintf(error, capacity, "%s", e.what());
        return 0;
    } catch (...) {
        return 0;
    }
}
int32_t FORGE_SDK_CALL controls(const ForgeSdkWorldV1* h, char* error, uint32_t capacity) {
    try {
        forge::sdk::Client client(h);
        flecs::world world(h->world);
        auto state = world.get<PendingFeedback>();
        if (state.token) {
            const auto receipt = read_json([&](char* data, uint32_t size) {
                return h->game_inspect(h->context, state.token, data, size);
            });
            if (receipt.at("state") != "queued") {
                const bool failed = receipt.at("state") == "failed";
                world.each([&](FeedbackProbe& probe) {
                    ++probe.completed;
                    if (failed)
                        ++probe.failed;
                });
                if (!h->game_release(h->context, state.token))
                    throw std::runtime_error("Feedback receipt release failed");
                state.token = 0;
            }
        }
        if (!state.token) {
            ForgeSdkActionV1 pulse{}, stop{};
            client.control("12345678-1234-4234-8234-123456789b01", pulse);
            client.control("12345678-1234-4234-8234-123456789b02", stop);
            if (pulse.pressed || stop.pressed) {
                const auto command = Json{{"operation", "rumble"},
                                          {"low", stop.pressed ? 0.0 : .5},
                                          {"high", stop.pressed ? 0.0 : .75},
                                          {"duration_ms", stop.pressed ? 0 : 1000}}
                                         .dump();
                state.token = h->game_request(h->context, command.c_str());
                if (!state.token)
                    throw std::runtime_error("Feedback request rejected");
                world.each([](FeedbackProbe& probe) { ++probe.requested; });
            }
        }
        world.set(state);
        return 1;
    } catch (const std::exception& e) {
        if (capacity)
            std::snprintf(error, capacity, "%s", e.what());
        return 0;
    } catch (...) {
        return 0;
    }
}
const char* dependencies[]{"forge.input", "forge.game", "forge.transforms"};
const ForgeNativeSdkV1 api{sizeof(ForgeNativeSdkV1),
                           FORGE_NATIVE_SDK_ABI,
                           FORGE_NATIVE_SDK_FINGERPRINT,
                           "project.gamepad_feedback",
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
