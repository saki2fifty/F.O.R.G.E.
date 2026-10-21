#include <box2d/box2d.h>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
void require(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}
struct World {
    b2WorldId id;
    explicit World(b2WorldId value) : id(value) {}
    World(const World&) = delete;
    ~World() {
        if (B2_IS_NON_NULL(id))
            b2DestroyWorld(id);
    }
};
struct Found {
    b2BodyId body{};
    unsigned count{};
};
bool collect(b2ShapeId shape, void* context) noexcept {
    auto& found = *static_cast<Found*>(context);
    found.body = b2Shape_GetBody(shape);
    return ++found.count < 2;
}
void native_profile() {
    static_assert(sizeof(b2Pos) == 2 * sizeof(double));
    require(b2IsDoublePrecision(), "Linked Box2D precision differs from its private headers");
    const auto version = b2GetVersion();
    require(version.major == 3 && version.minor == 2 && version.revision == 0,
            "Unexpected Box2D API version");
    auto definition = b2DefaultWorldDef();
    definition.gravity = {0, 0};
    definition.workerCount = 1;
    World original(b2CreateWorld(&definition));
    require(B2_IS_NON_NULL(original.id), "Native 2D world creation failed");
    int application_hook = 7;
    auto body = b2DefaultBodyDef();
    body.type = b2_dynamicBody;
    body.position = {1000000, 0};
    body.linearVelocity = {.25f, 0};
    body.name = "0000000001";
    body.userData = &application_hook;
    const auto actor = b2CreateBody(original.id, &body);
    auto shape = b2DefaultShapeDef();
    shape.userData = &application_hook;
    const b2Circle circle{{0, 0}, .5f};
    require(b2Shape_IsValid(b2CreateCircleShape(actor, &shape, &circle)),
            "Native inline shape creation failed");
    auto mass = b2Body_GetMassData(actor);
    require(std::abs(mass.mass - .25 * std::acos(-1.)) < 1e-6,
            "Native density is not mass per square metre");
    const float ratio = 2 / mass.mass;
    mass.mass = 2;
    mass.rotationalInertia *= ratio;
    b2Body_SetMassData(actor, mass);
    const auto overridden = b2Body_GetMassData(actor);
    require(std::abs(overridden.mass - 2) < 1e-6 &&
                std::abs(overridden.rotationalInertia - .25f) < 1e-6,
            "Native mass override did not preserve proportional inertia");
    for (int tick = 0; tick < 60; ++tick)
        b2World_Step(original.id, 1.f / 60, 4);
    require(std::abs(b2Body_GetPosition(actor).x - 1000000.25) < 1e-6,
            "Native world positions lost sub-float-step motion");
    const int required = b2World_GetSnapshot(original.id, nullptr, 0);
    require(required > 0 && required <= 2 * 1024 * 1024, "Native profile snapshot exceeds budget");
    std::vector<std::uint8_t> bytes(std::size_t(required), 0);
    require(b2World_GetSnapshot(original.id, bytes.data(), required) == required,
            "Native snapshot capture failed");
    World candidate(b2CreateWorldFromSnapshot(bytes.data(), required, 1));
    require(B2_IS_NON_NULL(candidate.id) &&
                b2StoreWorldId(candidate.id) != b2StoreWorldId(original.id),
            "Native snapshot did not create a separate candidate");
    Found found;
    b2World_OverlapAABB(candidate.id, {1000000, 0}, {{-4, -4}, {4, 4}}, b2DefaultQueryFilter(),
                        collect, &found);
    require(found.count == 1 && b2Body_IsValid(found.body), "Native candidate binding is missing");
    require(std::strcmp(b2Body_GetName(found.body), "0000000001") == 0 &&
                b2Body_GetUserData(found.body) == nullptr,
            "Native snapshot tag or application-pointer contract differs");
    const auto restored_mass = b2Body_GetMassData(found.body);
    require(std::abs(restored_mass.mass - 2) < 1e-6, "Native candidate lost mass override");
    for (int tick = 0; tick < 30; ++tick) {
        b2World_Step(original.id, 1.f / 60, 4);
        b2World_Step(candidate.id, 1.f / 60, 4);
        require(std::abs(b2Body_GetPosition(actor).x - b2Body_GetPosition(found.body).x) < 1e-9,
                "Native candidate continuation differs");
    }
    require(b2Body_GetUserData(actor) == &application_hook,
            "Candidate operations modified original application hooks");
    std::cout << "Native 2D dependency profile passed; snapshot " << required
              << " bytes; AVX2 available " << b2IsAVX2Available()
              << ". No FORGE 2D body/recovery integration claimed.\n";
}
} // namespace
int main() {
    try {
        native_profile();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
