// Original exact-SDK copied ray acceptance; no solver headers or native handles.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <flecs.h>
#include <forge/native_sdk_identity.h>
#include <forge/sdk_client.hpp>
#include <limits>
#include <stdexcept>
#include <thread>
namespace {
void require(bool value, const char* text) {
    if (!value)
        throw std::runtime_error(text);
}
template <class Hit> Hit dirty() {
    Hit hit{};
    hit.size = sizeof(hit);
    hit.scene[0] = hit.entity[0] = 'x';
    hit.fraction = 42;
    hit.position[0] = hit.normal[0] = 42;
    return hit;
}
template <class Hit> bool cleared(const Hit& hit) {
    Hit empty{};
    empty.size = sizeof(empty);
    return hit.size == empty.size && !hit.scene[0] && !hit.entity[0] && !hit.fraction &&
           std::all_of(std::begin(hit.position), std::end(hit.position),
                       [](double v) { return v == 0; }) &&
           std::all_of(std::begin(hit.normal), std::end(hit.normal),
                       [](double v) { return v == 0; });
}
template <std::size_t N, class Hit> void prove(const ForgeSdkWorldV1* host) {
    std::array<double, N> from{}, delta{};
    from[1] = 5;
    delta[1] = -10;
    std::array<Hit, 4> hits;
    const auto raw = [&](const double* origin, const double* displacement, uint32_t mask,
                         uint32_t sensors, Hit* out, uint32_t capacity) {
        if constexpr (N == 2)
            return host->physics_raycast_all_2d(host->context, origin, displacement, mask, sensors,
                                                out, capacity);
        else
            return host->physics_raycast_all(host->context, origin, displacement, mask, sensors,
                                             out, capacity);
    };
    const auto client = [&](const forge::sdk::Client& access) {
        if constexpr (N == 2)
            return access.raycast_all_2d(from, delta, UINT32_MAX, true, hits.data(), hits.size());
        else
            return access.raycast_all(from, delta, UINT32_MAX, true, hits.data(), hits.size());
    };
    hits.fill(dirty<Hit>());
#ifdef FORGE_TEST_RAYS_DENIED
    require(raw(from.data(), delta.data(), UINT32_MAX, 1, hits.data(), hits.size()) == -1 &&
                std::all_of(hits.begin(), hits.end(), cleared<Hit>),
            "Multi-hit ray exposed native data without Physics permission");
    require(client(forge::sdk::Client(host)) == -1 &&
                std::all_of(hits.begin(), hits.end(), cleared<Hit>),
            "Client exposed multi-hit rays without Physics permission");
#else
    require(raw(from.data(), delta.data(), UINT32_MAX, 1, hits.data(), hits.size()) == 3 &&
                cleared(hits.back()),
            "Multi-hit SDK lost complete results/unused clearing");
    for (unsigned i = 0; i < 3; ++i) {
        const double fraction = i == 0 ? .15 : i == 1 ? .25 : .45;
        require(std::strlen(hits[i].scene) == 36 && std::strlen(hits[i].entity) == 36 &&
                    std::strcmp(hits[0].scene, hits[i].scene) == 0 &&
                    std::abs(hits[i].fraction - fraction) < 1e-6 && hits[i].normal[1] > .99 &&
                    std::abs(hits[i].position[1] - (5 - 10 * fraction)) < 1e-6,
                "Multi-hit SDK surface geometry/order/UUID copy is wrong");
        if (i)
            require(std::strcmp(hits[i - 1].entity, hits[i].entity) != 0,
                    "Multi-hit SDK duplicated canonical objects");
    }
    require(client(forge::sdk::Client(host)) == 3, "C++ client ray failed");
    require(raw(from.data(), delta.data(), UINT32_MAX, 0, hits.data(), hits.size()) == 2 &&
                cleared(hits[2]) && cleared(hits[3]),
            "SDK ray sensor filtering failed");
    require(raw(from.data(), delta.data(), 0, 1, hits.data(), hits.size()) == 0 &&
                std::all_of(hits.begin(), hits.end(), cleared<Hit>),
            "SDK ray miss did not clear");
    hits.fill(dirty<Hit>());
    require(raw(from.data(), delta.data(), UINT32_MAX, 1, hits.data(), 2) == -2 &&
                cleared(hits[0]) && cleared(hits[1]) && hits[2].fraction == 42,
            "SDK ray capacity returned partial success or wrote beyond caller capacity");
    hits.fill(dirty<Hit>());
    --hits[1].size;
    const auto invalid = hits;
    require(raw(from.data(), delta.data(), UINT32_MAX, 1, hits.data(), hits.size()) == -1 &&
                std::memcmp(hits.data(), invalid.data(), sizeof(hits)) == 0,
            "SDK ray wrote an array containing a wrong-size slot");
    hits.fill(dirty<Hit>());
    const auto saved = hits;
    require(raw(from.data(), delta.data(), UINT32_MAX, 1, hits.data(), 0) == -1 &&
                raw(from.data(), delta.data(), UINT32_MAX, 1, hits.data(), 257) == -1 &&
                raw(from.data(), delta.data(), UINT32_MAX, 1, nullptr, 1) == -1 &&
                std::memcmp(hits.data(), saved.data(), sizeof(hits)) == 0,
            "SDK ray invalid capacity wrote caller memory");
    for (unsigned mode = 0; mode < 4; ++mode) {
        hits.fill(dirty<Hit>());
        auto origin = from;
        if (mode == 3)
            origin[0] = std::numeric_limits<double>::quiet_NaN();
        require(raw(mode == 0 ? nullptr : origin.data(), mode == 1 ? nullptr : delta.data(),
                    UINT32_MAX, mode == 2 ? 2 : 1, hits.data(), hits.size()) == -1 &&
                    std::all_of(hits.begin(), hits.end(), cleared<Hit>),
                "SDK ray invalid arguments left stale outputs");
    }
    hits.fill(dirty<Hit>());
    std::copy(from.begin(), from.end(), hits[0].position);
    std::copy(delta.begin(), delta.end(), hits[0].normal);
    require(raw(hits[0].position, hits[0].normal, UINT32_MAX, 1, hits.data(), hits.size()) == 3 &&
                std::abs(hits[0].fraction - .15) < 1e-6,
            "SDK ray input/output alias failed");
    bool rejected = false;
    std::thread foreign([&] {
        std::array<Hit, 4> values;
        values.fill(dirty<Hit>());
        rejected =
            raw(from.data(), delta.data(), UINT32_MAX, 1, values.data(), values.size()) == -1 &&
            std::all_of(values.begin(), values.end(), cleared<Hit>);
    });
    foreign.join();
    require(rejected, "SDK ray admitted a foreign thread");
#endif
    hits.fill(dirty<Hit>());
    require(client(forge::sdk::Client(nullptr)) == -1 &&
                std::all_of(hits.begin(), hits.end(), cleared<Hit>),
            "Unavailable C++ ray client left stale output");
}
int32_t FORGE_SDK_CALL schemas(const ForgeSdkWorldV1*, char*, uint32_t) { return 1; }
int32_t FORGE_SDK_CALL start(const ForgeSdkWorldV1* host, char*, uint32_t) {
    flecs::world(host->world)
        .system<>()
        .kind(host->post_physics_phase)
        .run([host](flecs::iter&) {
            prove<3, ForgeSdkPhysicsHitV1>(host);
            prove<2, ForgeSdkPhysicsHit2DV1>(host);
            require(host->diagnostic(host->context, 1, "Multi-hit ray SDK proof passed") == 1,
                    "Ray proof diagnostic rejected");
        })
        .add(host->fixed_tag);
    return 1;
}
int32_t FORGE_SDK_CALL ready(const ForgeSdkWorldV1* host, char* error, uint32_t capacity) {
    auto a = dirty<ForgeSdkPhysicsHitV1>();
    auto b = dirty<ForgeSdkPhysicsHit2DV1>();
    const double from[] = {0, 5, 0}, delta[] = {0, -10, 0};
    if (host->physics_raycast_all(host->context, from, delta, UINT32_MAX, 1, &a, 1) != -1 ||
        host->physics_raycast_all_2d(host->context, from, delta, UINT32_MAX, 1, &b, 1) != -1 ||
        !cleared(a) || !cleared(b)) {
        if (capacity)
            std::snprintf(error, capacity, "Multi-hit rays admitted a nonfixed callback");
        return 0;
    }
    return 1;
}
const char* dependencies[] = {"forge.physics"};
#ifdef FORGE_TEST_RAYS_DENIED
constexpr auto module_id = "project.physics_rays_denied";
constexpr uint64_t permissions = FORGE_SDK_DIAGNOSTICS;
#else
constexpr auto module_id = "project.physics_rays_probe";
constexpr uint64_t permissions = FORGE_SDK_DIAGNOSTICS | FORGE_SDK_PHYSICS;
#endif
const ForgeNativeSdkV1 api = {sizeof(ForgeNativeSdkV1),
                              FORGE_NATIVE_SDK_ABI,
                              FORGE_NATIVE_SDK_FINGERPRINT,
                              module_id,
                              "1",
                              dependencies,
                              1,
                              FORGE_SDK_RUNTIME,
                              FORGE_SDK_RUNTIME,
                              0,
                              permissions,
                              &ecs_init,
                              &ecs_os_api,
                              schemas,
                              start,
                              nullptr,
                              nullptr,
                              nullptr,
                              ready};
} // namespace
extern "C" FORGE_SDK_EXPORT const ForgeNativeSdkV1* FORGE_SDK_CALL forge_native_sdk_v1() {
    return &api;
}
