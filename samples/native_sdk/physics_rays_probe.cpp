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
ForgeSdkEntityRefV1 dirty_reference() {
    ForgeSdkEntityRefV1 ref{};
    ref.size = sizeof(ref);
    ref.version = 42;
    ref.scene_uuid[0] = ref.entity_uuid[0] = 'x';
    return ref;
}
bool cleared_reference(const ForgeSdkEntityRefV1& ref) {
    return ref.size == sizeof(ref) && !ref.version && !ref.scene_uuid[0] && !ref.entity_uuid[0];
}
template <std::size_t N, class Hit, class Overlap, class Sweep>
void prove_exclusions(const ForgeSdkWorldV1* host, bool fixed = true) {
    std::array<double, N> from{}, delta{};
    from[1] = 5;
    delta[1] = -10;
    Overlap overlap{};
    overlap.size = sizeof(overlap);
    overlap.shape = 1;
    overlap.layer_mask = UINT32_MAX;
    overlap.include_sensors = 1;
    overlap.dimensions[0] = 5;
    overlap.origin[1] = 1.5;
    Sweep sweep{};
    sweep.size = sizeof(sweep);
    sweep.shape = 1;
    sweep.layer_mask = UINT32_MAX;
    sweep.include_sensors = 1;
    sweep.dimensions[0] = .1;
    std::copy(from.begin(), from.end(), sweep.origin);
    std::copy(delta.begin(), delta.end(), sweep.displacement);
    if constexpr (N == 3) {
        overlap.rotation[3] = 1;
        sweep.rotation[3] = 1;
    }
    std::array<Hit, 4> hits;
    std::array<ForgeSdkEntityRefV1, 4> refs;
    const auto reset = [&] {
        hits.fill(dirty<Hit>());
        refs.fill(dirty_reference());
    };
    std::array<ForgeSdkEntityRefV1, 2> ignored{};
    for (auto& ref : ignored) {
        ref.size = sizeof(ref);
        ref.version = 1;
        std::strcpy(ref.scene_uuid, "11111111-1111-4111-8111-111111111111");
        std::strcpy(ref.entity_uuid, "22222222-2222-4222-8222-222222222222");
    }
    ForgeSdkPhysicsExclusionsV1 input{sizeof(input), uint32_t(ignored.size()), ignored.data()};
    const auto raw = [&](unsigned kind, const ForgeSdkPhysicsExclusionsV1* excluded,
                         uint32_t capacity = 4) {
        if constexpr (N == 2) {
            if (kind == 0)
                return host->physics_raycast_2d_excluding(host->context, from.data(), delta.data(),
                                                          UINT32_MAX, 1, excluded, hits.data());
            if (kind == 1)
                return host->physics_raycast_all_2d_excluding(host->context, from.data(),
                                                              delta.data(), UINT32_MAX, 1, excluded,
                                                              hits.data(), capacity);
            if (kind == 2)
                return host->physics_overlap_shape_2d_excluding(host->context, &overlap, excluded,
                                                                refs.data(), capacity);
            return host->physics_shape_cast_2d_excluding(host->context, &sweep, excluded,
                                                         hits.data());
        } else {
            if (kind == 0)
                return host->physics_raycast_excluding(host->context, from.data(), delta.data(),
                                                       UINT32_MAX, 1, excluded, hits.data());
            if (kind == 1)
                return host->physics_raycast_all_excluding(host->context, from.data(), delta.data(),
                                                           UINT32_MAX, 1, excluded, hits.data(),
                                                           capacity);
            if (kind == 2)
                return host->physics_overlap_shape_excluding(host->context, &overlap, excluded,
                                                             refs.data(), capacity);
            return host->physics_shape_cast_excluding(host->context, &sweep, excluded, hits.data());
        }
    };
    const auto client = [&](unsigned kind, const forge::sdk::Client& access,
                            std::span<const ForgeSdkEntityRefV1> excluded) {
        if constexpr (N == 2) {
            if (kind == 0)
                return access.raycast_2d_excluding(from, delta, UINT32_MAX, true, excluded,
                                                   hits[0]);
            if (kind == 1)
                return access.raycast_all_2d_excluding(from, delta, UINT32_MAX, true, excluded,
                                                       hits.data(), hits.size());
            if (kind == 2)
                return access.overlap_shape_2d_excluding(overlap, excluded, refs.data(),
                                                         refs.size());
            return access.shape_cast_2d_excluding(sweep, excluded, hits[0]);
        } else {
            if (kind == 0)
                return access.raycast_excluding(from, delta, UINT32_MAX, true, excluded, hits[0]);
            if (kind == 1)
                return access.raycast_all_excluding(from, delta, UINT32_MAX, true, excluded,
                                                    hits.data(), hits.size());
            if (kind == 2)
                return access.overlap_shape_excluding(overlap, excluded, refs.data(), refs.size());
            return access.shape_cast_excluding(sweep, excluded, hits[0]);
        }
    };
    const auto cleared_outputs = [&](unsigned kind) {
        if (kind == 2)
            return std::all_of(refs.begin(), refs.end(), cleared_reference);
        return kind == 1 ? std::all_of(hits.begin(), hits.end(), cleared<Hit>) : cleared(hits[0]);
    };
    if (!fixed) {
        for (unsigned kind = 0; kind < 4; ++kind) {
            reset();
            require(raw(kind, nullptr) == -1 && cleared_outputs(kind),
                    "Excluded query accepted a nonfixed callback");
        }
        return;
    }
#ifdef FORGE_TEST_RAYS_DENIED
    for (unsigned kind = 0; kind < 4; ++kind) {
        reset();
        require(raw(kind, &input) == -1 && cleared_outputs(kind),
                "Excluded query exposed physics without permission");
        reset();
        require(client(kind, forge::sdk::Client(host), ignored) == -1 && cleared_outputs(kind),
                "Excluded C++ query exposed physics without permission");
    }
#else
    for (unsigned kind = 0; kind < 4; ++kind) {
        reset();
        const auto expected = kind == 1 || kind == 2 ? 3 : 1;
        require(raw(kind, &input) == expected, "Unavailable exclusions changed existing objects");
        require(raw(kind, nullptr) == expected, "Null exclusion descriptor changed ordinary query");
    }
    forge::sdk::Client access(host);
    require(client(1, access, {}) == 3, "Empty C++ exclusion path lost ordinary rays");
    for (unsigned i = 0; i < ignored.size(); ++i) {
        std::memcpy(ignored[i].scene_uuid, hits[i].scene, 37);
        std::memcpy(ignored[i].entity_uuid, hits[i].entity, 37);
    }
    const auto check_hit = [&](unsigned kind) {
        if (kind == 2) {
            require(refs[0].version == 1 && std::strlen(refs[0].scene_uuid) == 36 &&
                        std::strlen(refs[0].entity_uuid) == 36 && cleared_reference(refs[1]) &&
                        cleared_reference(refs[2]) && cleared_reference(refs[3]),
                    "Excluded volume lost complete identities or unused clearing");
            require(std::strcmp(refs[0].entity_uuid, ignored[0].entity_uuid) &&
                        std::strcmp(refs[0].entity_uuid, ignored[1].entity_uuid),
                    "Excluded volume retained an ignored object");
        } else {
            require(std::strcmp(hits[0].entity, ignored[0].entity_uuid) &&
                        std::strcmp(hits[0].entity, ignored[1].entity_uuid) &&
                        hits[0].normal[1] > .99 && hits[0].fraction > .42 && hits[0].fraction < .46,
                    "Excluded query lost the farther surface geometry");
            if (kind != 3)
                require(std::abs(hits[0].fraction - .45) < 1e-6,
                        "Excluded ray fraction disagrees with known geometry");
            if (kind == 1)
                require(cleared(hits[1]) && cleared(hits[2]) && cleared(hits[3]),
                        "Excluded multi-hit ray did not clear unused slots");
        }
    };
    for (unsigned kind = 0; kind < 4; ++kind) {
        reset();
        require(raw(kind, &input) == 1, "Canonical exclusion did not remove precisely two objects");
        check_hit(kind);
        reset();
        require(client(kind, access, ignored) == 1, "Excluded C++ query failed");
        check_hit(kind);
        reset();
        require(client(kind, access, {}) == (kind == 1 || kind == 2 ? 3 : 1),
                "Empty C++ exclusion did not preserve ordinary query access");
    }
    // Same typed input/output rows: input must survive the output clear.
    reset();
    refs[0] = ignored[0];
    refs[1] = ignored[1];
    auto alias = input;
    alias.entities = refs.data();
    require(raw(2, &alias) == 1, "Excluded UUID input/output array alias failed");
    check_hit(2);
    reset();
    refs[0] = ignored[0];
    refs[1] = ignored[1];
    require(client(2, access, {refs.data(), 2}) == 1,
            "C++ excluded UUID input/output alias failed");
    check_hit(2);
    for (unsigned mode = 0; mode < 7; ++mode) {
        auto invalid = input;
        auto bad_refs = ignored;
        invalid.entities = bad_refs.data();
        if (mode == 0)
            --invalid.size;
        else if (mode == 1)
            invalid.count = FORGE_SDK_PHYSICS_EXCLUSION_LIMIT + 1;
        else if (mode == 2)
            invalid.entities = nullptr;
        else if (mode == 3)
            --bad_refs[0].size;
        else if (mode == 4)
            bad_refs[0].version = 2;
        else if (mode == 5)
            bad_refs[0].scene_uuid[0] = 0;
        else
            bad_refs[0].entity_uuid[36] = 'x';
        for (unsigned kind = 0; kind < 4; ++kind) {
            reset();
            require(raw(kind, &invalid) == -1 && cleared_outputs(kind),
                    "Excluded query accepted invalid descriptor/ref or retained stale output");
        }
    }
    for (unsigned kind = 0; kind < 4; ++kind) {
        reset();
        if (kind == 2) {
            --refs[1].size;
            require(raw(kind, &input) == -1 && refs[0].version == 42 &&
                        refs[1].size == sizeof(refs[1]) - 1,
                    "Excluded query wrote an invalid output array");
        } else {
            const auto slot = kind == 1 ? 1 : 0;
            --hits[slot].size;
            require(raw(kind, &input) == -1 && hits[0].fraction == 42 &&
                        hits[slot].size == sizeof(hits[slot]) - 1,
                    "Excluded query wrote a wrong-size hit buffer");
        }
    }
    for (unsigned kind : {1u, 2u}) {
        reset();
        require(raw(kind, nullptr, 1) == -2 &&
                    (kind == 1 ? cleared(hits[0]) && hits[1].fraction == 42
                               : cleared_reference(refs[0]) && refs[1].version == 42),
                "Excluded entry point returned partial output or wrote beyond capacity");
        reset();
        require(raw(kind, &input, 257) == -1 &&
                    (kind == 1 ? hits[0].fraction == 42 : refs[0].version == 42),
                "Excluded entry point touched an oversized caller buffer");
    }
    reset();
    require(raw(0, &input) == 1, "Missing final target for full-exclusion proof");
    std::array<ForgeSdkEntityRefV1, 3> all{ignored[0], ignored[1], ignored[0]};
    std::memcpy(all[2].scene_uuid, hits[0].scene, 37);
    std::memcpy(all[2].entity_uuid, hits[0].entity, 37);
    const ForgeSdkPhysicsExclusionsV1 complete{sizeof(complete), uint32_t(all.size()), all.data()};
    for (unsigned kind = 0; kind < 4; ++kind) {
        reset();
        require(raw(kind, &complete) == 0 && cleared_outputs(kind),
                "All-excluded query fabricated a hit or retained output");
    }
    if constexpr (N == 2) {
        std::memcpy(sweep.ignore_scene, all[2].scene_uuid, 37);
        std::memcpy(sweep.ignore_entity, all[2].entity_uuid, 37);
        reset();
        require(raw(3, &input) == 0 && cleared_outputs(3),
                "New exclusions replaced legacy2D sweep ignore");
        sweep.ignore_scene[0] = sweep.ignore_entity[0] = 0;
    }
    reset();
    std::array<ForgeSdkEntityRefV1, FORGE_SDK_PHYSICS_EXCLUSION_LIMIT + 1> repeated;
    repeated.fill(ignored[0]);
    auto duplicate = input;
    duplicate.count = FORGE_SDK_PHYSICS_EXCLUSION_LIMIT;
    duplicate.entities = repeated.data();
    require(raw(1, &duplicate) == 2, "Exact/duplicate exclusion list rejected");
    reset();
    require(client(1, access, repeated) == -1 && cleared_outputs(1),
            "Oversized C++ exclusion list left stale output");
    bool rejected = false;
    std::thread foreign([&] {
        reset();
        rejected = raw(0, &input) == -1 && cleared_outputs(0);
    });
    foreign.join();
    require(rejected, "Excluded query accepted a foreign owner thread");
    // Legacy3D filtered/unfiltered ray and both C++ closest helpers must snapshot
    // actual aliased double members before clearing their output record.
    for (unsigned mode = 0; mode < 2; ++mode) {
        reset();
        std::copy(from.begin(), from.end(), hits[0].position);
        std::copy(delta.begin(), delta.end(), hits[0].normal);
        int32_t result;
        if constexpr (N == 2) {
            result = mode == 0
                         ? host->physics_raycast_2d(host->context, hits[0].position, hits[0].normal,
                                                    UINT32_MAX, 1, hits.data())
                         : access.raycast_2d(hits[0].position, hits[0].normal, UINT32_MAX, true,
                                             hits[0]);
        } else {
            result = mode == 0 ? host->raycast_filtered(host->context, hits[0].position,
                                                        hits[0].normal, UINT32_MAX, 1, hits.data())
                               : access.raycast_filtered(hits[0].position, hits[0].normal,
                                                         UINT32_MAX, true, hits[0]);
        }
        require(result == 1 && std::abs(hits[0].fraction - .15) < 1e-6,
                "Existing closest query lost aliased input geometry");
    }
    if constexpr (N == 3) {
        reset();
        std::copy(from.begin(), from.end(), hits[0].position);
        std::copy(delta.begin(), delta.end(), hits[0].normal);
        require(host->raycast(host->context, hits[0].position, hits[0].normal, hits.data()) == 1 &&
                    std::abs(hits[0].fraction - .15) < 1e-6,
                "Legacy unfiltered ray read cleared input");
        reset();
        std::copy(from.begin(), from.end(), hits[0].position);
        std::copy(delta.begin(), delta.end(), hits[0].normal);
        require(access.raycast(hits[0].position, hits[0].normal, hits[0]) == 1 &&
                    std::abs(hits[0].fraction - .15) < 1e-6,
                "Legacy C++ unfiltered ray read cleared input");
    }
#endif
    for (unsigned kind = 0; kind < 4; ++kind) {
        reset();
        require(client(kind, forge::sdk::Client(nullptr), ignored) == -1 && cleared_outputs(kind),
                "Unavailable excluded C++ query left stale outputs");
    }
}
int32_t FORGE_SDK_CALL schemas(const ForgeSdkWorldV1*, char*, uint32_t) { return 1; }
int32_t FORGE_SDK_CALL start(const ForgeSdkWorldV1* host, char*, uint32_t) {
    flecs::world(host->world)
        .system<>()
        .kind(host->post_physics_phase)
        .run([host](flecs::iter&) {
            prove<3, ForgeSdkPhysicsHitV1>(host);
            prove<2, ForgeSdkPhysicsHit2DV1>(host);
            prove_exclusions<3, ForgeSdkPhysicsHitV1, ForgeSdkOverlapV1, ForgeSdkSweepV1>(host);
            prove_exclusions<2, ForgeSdkPhysicsHit2DV1, ForgeSdkOverlap2DV1, ForgeSdkSweep2DV1>(
                host);
            require(host->diagnostic(host->context, 1, "Multi-hit ray SDK proof passed") == 1,
                    "Ray proof diagnostic rejected");
        })
        .add(host->fixed_tag);
    return 1;
}
int32_t FORGE_SDK_CALL ready(const ForgeSdkWorldV1* host, char* error, uint32_t capacity) {
    try {
        prove_exclusions<3, ForgeSdkPhysicsHitV1, ForgeSdkOverlapV1, ForgeSdkSweepV1>(host, false);
        prove_exclusions<2, ForgeSdkPhysicsHit2DV1, ForgeSdkOverlap2DV1, ForgeSdkSweep2DV1>(host,
                                                                                            false);
    } catch (const std::exception& e) {
        if (capacity)
            std::snprintf(error, capacity, "%s", e.what());
        return 0;
    }
    auto a = dirty<ForgeSdkPhysicsHitV1>();
    auto b = dirty<ForgeSdkPhysicsHit2DV1>();
    const double from[] = {0, 5, 0}, delta[] = {0, -10, 0};
    if (host->raycast(host->context, from, delta, &a) != -1 || !cleared(a)) {
        if (capacity)
            std::snprintf(error, capacity, "Legacy unfiltered ray retained a nonfixed output");
        return 0;
    }
    a = dirty<ForgeSdkPhysicsHitV1>();
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
