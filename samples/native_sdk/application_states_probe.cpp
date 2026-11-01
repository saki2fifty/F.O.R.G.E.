// Independently authored example of world-local state conditions and host requests.
#include <cstdio>
#include <flecs.h>
#include <forge/application_state_ecs.hpp>
#include <forge/native_sdk_identity.h>
#include <forge/sdk_client.hpp>
#include <memory>
#include <nlohmann/json.hpp>
#include <vector>

namespace {
constexpr const char* domain = "application_session_with_owned_strings";
constexpr const char* menu = "menu_with_owned_storage";
constexpr const char* playing = "play_with_owned_storage";
constexpr const char* parent = "game_with_owned_storage";
struct Probe {
    std::uint64_t ticks = 0, conditional_ticks = 0, token = 0;
};
struct Events {
    std::vector<forge::ApplicationStateEntered> entered;
    std::vector<forge::ApplicationStateExited> exited;
    bool failed = false;
};
void require(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
int32_t FORGE_SDK_CALL schemas(const ForgeSdkWorldV1* host, char* error, uint32_t capacity) {
    try {
        flecs::world world(host->world);
        world.component<Probe>();
        (void)forge::application_state_domain(world, domain);
        (void)forge::application_state_entity(world, domain, parent);
        return 1;
    } catch (const std::exception& exception) {
        if (capacity)
            std::snprintf(error, capacity, "%s", exception.what());
        return 0;
    }
}
int32_t FORGE_SDK_CALL start(const ForgeSdkWorldV1* host, char* error, uint32_t capacity) {
    try {
        flecs::world world(host->world);
        const auto events = std::make_shared<Events>();
        world.observer()
            .event<forge::ApplicationStateEntered>()
            .with<forge::ApplicationStateDomainTag>()
            .run([host, events](flecs::iter& it) {
                while (it.next()) {
                    const auto& event = *it.param<forge::ApplicationStateEntered>();
                    events->entered.push_back(event);
                    if (event.domain == domain && event.state == playing) {
                        flecs::world current(host->world);
                        if (!forge::application_state_is(current, domain, parent) ||
                            !forge::application_state_is(current, "hud", "visible")) {
                            events->failed = true;
                            host->diagnostic(
                                host->context, 3,
                                "Application state SDK event saw partial configuration");
                        }
                    }
                }
            });
        world.observer()
            .event<forge::ApplicationStateExited>()
            .with<forge::ApplicationStateDomainTag>()
            .run([events](flecs::iter& it) {
                while (it.next())
                    events->exited.push_back(*it.param<forge::ApplicationStateExited>());
            });
        world.entity().set<Probe>({});
        const auto owner = forge::application_state_domain(world, domain);
        const auto condition = forge::application_state_entity(world, domain, parent);
        world.system<Probe>("project.StateCondition")
            .kind(host->fixed_phase)
            .with<forge::ApplicationStateMember>(condition)
            .src(owner)
            .each([](Probe& probe) { ++probe.conditional_ticks; })
            .add(host->fixed_tag);
        world.system<Probe>("project.StateRequests")
            .kind(host->fixed_phase)
            .each([host, events](Probe& probe) {
                if (events->failed || probe.ticks >= 5)
                    return;
                try {
                    ++probe.ticks;
                    flecs::world current(host->world);
                    const forge::sdk::Client client(host);
                    require(client.request_application_state("bad\"key", playing) == 0,
                            "Application state SDK accepted unsafe semantic key");
                    if (!client.available(forge::sdk::Capability::Game)) {
                        require(client.request_application_state(domain, playing) == 0,
                                "Application state SDK fabricated an unavailable Game service");
                        if (probe.ticks == 1)
                            require(probe.conditional_ticks == 0 &&
                                        forge::application_state_current(current, domain) == menu,
                                    "Application state SDK legacy initial condition leaked");
                        else if (probe.ticks == 2)
                            require(probe.conditional_ticks == 1 &&
                                        forge::application_state_is(current, domain, parent),
                                    "Application state SDK legacy host transition did not update "
                                    "native condition");
                        else if (probe.ticks == 3) {
                            require(probe.conditional_ticks == 1 &&
                                        forge::application_state_current(current, domain) == menu,
                                    "Application state SDK legacy condition ran after exit");
                            bool retained = false;
                            for (const auto& event : events->entered)
                                retained |= event.domain == domain && event.state == playing &&
                                            event.current == playing;
                            require(
                                retained && !events->exited.empty(),
                                "Application state SDK legacy event copies lost owning strings");
                            host->diagnostic(host->context, 1,
                                             "Application state SDK legacy conditions, owned "
                                             "events and unavailable Game guard passed");
                        }
                        return;
                    }
                    if (probe.token) {
                        const auto bytes =
                            host->game_inspect(host->context, probe.token, nullptr, 0);
                        require(bytes > 0 && bytes <= 65536,
                                "Application state SDK receipt unavailable");
                        std::vector<char> data(bytes);
                        require(host->game_inspect(host->context, probe.token, data.data(),
                                                   bytes) == bytes,
                                "Application state SDK receipt copy changed");
                        const auto receipt = nlohmann::json::parse(data.data());
                        const bool rejected = probe.ticks == 3 || probe.ticks == 4;
                        require(receipt.at("state") == (rejected ? "failed" : "succeeded"),
                                "Application state SDK receipt has unexpected result");
                        require(host->game_release(host->context, probe.token) == 1,
                                "Application state SDK receipt did not release");
                        probe.token = 0;
                    }
                    if (probe.ticks == 1) {
                        require(probe.conditional_ticks == 0 &&
                                    forge::application_state_current(current, domain) == menu,
                                "Application state SDK leaked state across Worlds or ignored "
                                "initial condition");
                        probe.token = client.request_application_state(domain, playing, 1);
                    } else if (probe.ticks == 2) {
                        require(probe.conditional_ticks == 1 &&
                                    forge::application_state_is(current, domain, parent),
                                "Application state SDK native conditioned system did not run");
                        probe.token =
                            client.request_application_state(domain, menu, 1); // Stale revision.
                    } else if (probe.ticks == 3) {
                        require(probe.conditional_ticks == 2 &&
                                    forge::application_state_current(current, domain) == playing,
                                "Application state SDK stale request mutated active state");
                        probe.token =
                            client.request_application_state("hud", "hidden"); // Computed domain.
                    } else if (probe.ticks == 4) {
                        require(probe.conditional_ticks == 3,
                                "Application state SDK computed rejection changed condition");
                        probe.token = client.request_application_state(domain, menu, 2);
                    } else {
                        require(probe.conditional_ticks == 3 &&
                                    forge::application_state_current(current, domain) == menu,
                                "Application state SDK condition ran outside its active ancestor");
                        bool retained = false;
                        for (const auto& event : events->entered)
                            retained |= event.domain == domain && event.state == playing &&
                                        event.current == playing;
                        require(retained && !events->exited.empty(),
                                "Application state SDK owning event copies did not survive "
                                "transitions");
                        host->diagnostic(host->context, 1,
                                         "Application state SDK conditions, owned events, queued "
                                         "receipts and rejection passed");
                    }
                    require(probe.ticks == 5 || probe.token != 0,
                            "Application state SDK request did not queue");
                } catch (const std::exception& exception) {
                    events->failed = true;
                    host->diagnostic(host->context, 3, exception.what());
                }
            })
            .add(host->fixed_tag);
        return 1;
    } catch (const std::exception& exception) {
        if (capacity)
            std::snprintf(error, capacity, "%s", exception.what());
        return 0;
    }
}
const ForgeNativeSdkV1 api{sizeof(api),
                           FORGE_NATIVE_SDK_ABI,
                           FORGE_NATIVE_SDK_FINGERPRINT,
                           "project.application_states_probe",
                           "1",
                           nullptr,
                           0,
                           FORGE_SDK_RUNTIME,
                           FORGE_SDK_RUNTIME,
                           0,
                           FORGE_SDK_GAME | FORGE_SDK_DIAGNOSTICS,
                           &ecs_init,
                           &ecs_os_api,
                           schemas,
                           start,
                           nullptr};
} // namespace
extern "C" FORGE_SDK_EXPORT const ForgeNativeSdkV1* FORGE_SDK_CALL forge_native_sdk_v1() {
    return &api;
}
