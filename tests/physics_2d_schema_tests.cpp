#include "builtins.hpp"
#include <cstddef>
#include <filesystem>
#include <forge/scene.hpp>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string_view>

using namespace forge;
namespace {
void require(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}
template <class F> void rejects(F&& operation) {
    try {
        operation();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error("Invalid 2D authored scalar was accepted");
}
const detail::Builtin& spec(std::string_view name) {
    for (const auto& value : detail::builtins())
        if (name == value.name)
            return value;
    throw std::runtime_error("Missing builtin 2D component");
}
void native_layout(EngineContext& engine) {
    auto& world = engine.world().world();
    const auto type = world.lookup("forge.physics_body_2d");
    require(type, "2D body schema missing");
    const auto* info = ecs_get_type_info(world.c_ptr(), type.id());
    require(info && std::size_t(info->size) == sizeof(PhysicsBody2D), "2D body layout missing");
    for (const auto& [field, offset] :
         {std::pair{"motion", offsetof(PhysicsBody2D, motion)},
          std::pair{"density", offsetof(PhysicsBody2D, density)},
          std::pair{"mass", offsetof(PhysicsBody2D, mass)},
          std::pair{"friction", offsetof(PhysicsBody2D, friction)},
          std::pair{"restitution", offsetof(PhysicsBody2D, restitution)},
          std::pair{"gravity_factor", offsetof(PhysicsBody2D, gravity_factor)},
          std::pair{"enabled", offsetof(PhysicsBody2D, enabled)},
          std::pair{"sensor", offsetof(PhysicsBody2D, sensor)},
          std::pair{"layer", offsetof(PhysicsBody2D, layer)},
          std::pair{"mask", offsetof(PhysicsBody2D, mask)},
          std::pair{"linear_damping", offsetof(PhysicsBody2D, linear_damping)},
          std::pair{"angular_damping", offsetof(PhysicsBody2D, angular_damping)},
          std::pair{"allow_sleeping", offsetof(PhysicsBody2D, allow_sleeping)},
          std::pair{"lock_x", offsetof(PhysicsBody2D, lock_x)},
          std::pair{"lock_y", offsetof(PhysicsBody2D, lock_y)},
          std::pair{"lock_rotation", offsetof(PhysicsBody2D, lock_rotation)},
          std::pair{"bullet", offsetof(PhysicsBody2D, bullet)}}) {
        const auto* member = ecs_struct_get_member(world.c_ptr(), type.id(), field);
        require(member && std::size_t(member->offset) == offset,
                "2D native member offset mismatch");
    }
    for (const char* name :
         {"forge.box_collider_2d", "forge.circle_collider_2d", "forge.capsule_collider_2d"})
        require(world.lookup(name), "2D collider is outside the physics schema family");
    require(!engine.services().available(Capability::Physics), "Authored schema created a solver");
}
void scalar_admission(EngineContext& engine) {
    const auto& body = spec("forge.physics_body_2d");
    require(detail::canonical_builtin_value(body, Json::object()) == body.defaults,
            "Missing body fields did not backfill current defaults");
    detail::validate_components({{body.name, Json::object()}});
    detail::validate_components({{body.name, {{"density", .001}}}});
    for (const char* name : {"forge.physics_body_2d", "forge.box_collider_2d",
                             "forge.circle_collider_2d", "forge.capsule_collider_2d"}) {
        const auto& type = spec(name);
        detail::validate_components({{name, type.defaults}});
        auto entity = engine.world().world().entity();
        type.apply(entity, std::optional<detail::Value>{type.decode(type.defaults)});
        require(type.read(entity, false) == type.defaults,
                "Native 2D value codec changed defaults");
        entity.destruct();
    }
    auto boundary = engine.world().world().entity();
    detail::validate_reflected_value(boundary, BoxCollider2D{.000001f, .000001f});
    detail::validate_reflected_value(boundary, CircleCollider2D{.000001f});
    detail::validate_reflected_value(boundary, CapsuleCollider2D{.000001f, .000001f});
    PhysicsBody2D minimum_density;
    minimum_density.density = .001f;
    detail::validate_reflected_value(boundary, minimum_density);
    boundary.destruct();
    for (const auto& [field, value] : {std::pair<std::string, Json>{"motion", 3},
                                       {"motion", 1.25},
                                       {"density", 0},
                                       {"density", std::numeric_limits<double>::quiet_NaN()},
                                       {"mass", -1},
                                       {"mask", -1},
                                       {"mask", UINT64_MAX},
                                       {"layer", 32},
                                       {"gravity_factor", -11},
                                       {"lock_x", 1},
                                       {"bullet", "yes"},
                                       {"angular_damping", 10001}}) {
        auto bad = body.defaults;
        bad[field] = value;
        rejects([&] { detail::validate_components({{body.name, bad}}); });
    }
    auto reverse = body.defaults;
    reverse["gravity_factor"] = -1;
    detail::validate_components({{body.name, reverse}});
    for (const auto& [name, field] :
         {std::pair{"forge.box_collider_2d", "x"}, std::pair{"forge.circle_collider_2d", "radius"},
          std::pair{"forge.capsule_collider_2d", "height"}}) {
        auto bad = spec(name).defaults;
        bad[field] = 0;
        rejects([&] { detail::validate_components({{name, bad}}); });
    }
}
struct Cleanup {
    std::filesystem::path root;
    ~Cleanup() {
        std::error_code error;
        std::filesystem::remove_all(root, error);
    }
};
void source_round_trip(EngineContext& engine, const std::filesystem::path& parent) {
    Cleanup work{parent / ("case-" + AssetId::generate().str())};
    std::filesystem::create_directories(work.root);
    const auto asset = AssetId::generate();
    const auto id = EntityId::generate().str();
    auto settings = spec("forge.physics_body_2d").defaults;
    settings["motion"] = 2;
    settings["density"] = 2;
    settings["mass"] = 10;
    settings["gravity_factor"] = -1.25;
    settings["lock_x"] = true;
    settings["lock_rotation"] = true;
    settings["bullet"] = true;
    const Json opaque = {{"unsigned", UINT64_MAX}, {"unknown", Json::array({1, "keep", nullptr})}};
    settings["future_body_options"] = opaque;
    const Json row = {{"id", id},
                      {"name", "Planar source"},
                      {"spatial", {{"mode", "world"}}},
                      {"components",
                       {{"forge.local_translation", {{"x", 2}, {"y", 3}, {"z", 7}}},
                        {"forge.local_rotation", {{"x", 0}, {"y", 0}, {"z", 0}, {"w", 1}}},
                        {"forge.local_scale", {{"x", -2}, {"y", 2}, {"z", 0}}},
                        {"forge.physics_body_2d", settings},
                        {"forge.box_collider_2d", {{"x", 1}, {"y", 2}}}}}};
    Scene scene(engine.world());
    scene.reset({{"version", 5}, {"asset_id", asset}, {"entities", Json::array({row})}});
    const auto before = scene.snapshot();
    const auto ref = scene.reference(id);
    const auto value = scene.entity(id).get<PhysicsBody2D>();
    require(value.motion == 2 && value.lock_x && value.lock_rotation && value.bullet &&
                value.gravity_factor == -1.25f,
            "Native authored body differs from source");
    const auto path = work.root / "body.scene.json";
    scene.save(path);
    EngineContext reopened_engine(WorldRole::Authoring);
    Scene reopened(reopened_engine.world());
    reopened.load(path);
    require(reopened.reference(id) == ref && reopened.asset_id() == asset,
            "2D source reopening changed durable identity");
    require(reopened.entity(id).get<PhysicsBody2D>() == value &&
                reopened.entity(id).get<BoxCollider2D>() == BoxCollider2D{1, 2},
            "Reopened native 2D settings differ");
    require(reopened.entity(id).get<LocalTranslation>().z == 7 &&
                reopened.entity(id).get<LocalScale>() == LocalScale{-2, 2, 0},
            "2D source rewrote depth or signed/zero scale");
    require(reopened.snapshot() == before, "2D save/reopen lost source extensions or intent");
    auto invalid = before;
    invalid["entities"][0]["components"]["forge.physics_body_2d"]["density"] = -1;
    rejects([&] { reopened.reset(invalid); });
    require(reopened.snapshot() == before, "Rejected 2D source changed the admitted scene");
    require(!reopened_engine.services().available(Capability::Physics),
            "Reopening an authored 2D source created a native solver");
}
} // namespace
int main(int argc, char** argv) {
    try {
        require(argc == 2, "Expected a private schema fixture directory");
        EngineContext engine(WorldRole::Authoring);
        native_layout(engine);
        scalar_admission(engine);
        source_round_trip(engine, argv[1]);
        std::cout << "2D authored layout, scalar rejection, source/identity/extension round trip "
                     "passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
