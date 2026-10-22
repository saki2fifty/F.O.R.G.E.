#include "builtins.hpp"
#include <cstddef>
#include <filesystem>
#include <forge/authoring.hpp>
#include <forge/prefab_authoring.hpp>
#include <forge/scene.hpp>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string_view>

using namespace forge;
namespace {
struct BoundedLayerKey {
    std::uint64_t value = 0;
};
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
    const auto schema = detail::builtin_schema();
    bool found_motion = false;
    for (const auto& component : schema.at("components"))
        if (component.at("id") == "forge.physics_body_2d") {
            require(component.at("display_name") == "Physics Body 2D", "2D component picker label");
            for (const auto& field : component.at("fields"))
                if (field.at("id") == "motion") {
                    const auto& choices = field.at("choices");
                    require(choices.size() == 3 && choices[0].at("label") == "Static" &&
                                choices[1].at("label") == "Kinematic" &&
                                choices[2].at("label") == "Dynamic",
                            "2D motion did not reuse the existing named native enum");
                    found_motion = true;
                }
        }
    require(found_motion, "Missing Body2D motion choices");
}
void reflected_units(EngineContext& engine) {
    auto& world = engine.world().world();
    const auto schema = detail::builtin_schema();
    auto check = [&](const char* component, const char* field, ecs_entity_t expected,
                     const char* symbol) {
        const auto type = world.lookup(component);
        const auto member = type.lookup(field);
        require(expected && member && member.has<EcsMember>(), "Missing physics unit metadata");
        require(member.get<EcsMember>().unit == expected, "Physics member has the wrong unit");
        const auto* unit = ecs_get(world.c_ptr(), expected, EcsUnit);
        require(unit && unit->symbol && std::string_view(unit->symbol) == symbol,
                "Physics unit symbol mismatch");
        for (const auto& entry : schema.at("components"))
            if (entry.at("id") == component)
                for (const auto& property : entry.at("fields"))
                    if (property.at("id") == field) {
                        require(property.at("unit") == symbol,
                                "Inspector schema lost the reflected physics unit");
                        return;
                    }
        throw std::runtime_error("Physics field missing from Inspector schema");
    };
    const auto area_density = world.lookup("forge.units.KilogramsPerSquareMeter");
    const auto volume_density = world.lookup("forge.units.KilogramsPerCubicMeter");
    require(area_density && volume_density && area_density != volume_density,
            "Area density and volume density must use separate units");
    check("forge.physics_body_2d", "density", area_density, "kg/m^2");
    check("forge.physics_body_2d", "mass", EcsKiloGrams, "kg");
    check("forge.physics_body", "density", volume_density, "kg/m^3");
    check("forge.physics_body", "mass", EcsKiloGrams, "kg");
    check("forge.box_collider_2d", "x", EcsMeters, "m");
    check("forge.circle_collider_2d", "radius", EcsMeters, "m");
    check("forge.capsule_collider_2d", "height", EcsMeters, "m");
}
void scalar_admission(EngineContext& engine) {
    const auto& body = spec("forge.physics_body_2d");
    require(detail::canonical_builtin_value(body, Json::object()) == body.defaults,
            "Missing body fields did not backfill current defaults");
    detail::validate_components({{body.name, Json::object()}});
    detail::validate_components({{body.name, {{"density", .001}}}});
    for (const Json& source : {Json(nullptr), Json::array(), Json("invalid")})
        rejects([&] { detail::validate_components({{body.name, source}}); });
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
void controller_schema(EngineContext& engine) {
    const auto& type = spec("forge.character_controller_2d");
    auto& world = engine.world().world();
    const auto native = world.lookup(type.name);
    const auto* info = ecs_get_type_info(world.c_ptr(), native.id());
    require(info && std::size_t(info->size) == sizeof(CharacterController2D) &&
                std::size_t(info->alignment) == alignof(CharacterController2D),
            "2D controller has no matching reflected native layout");
    for (const auto& [name, offset] :
         {std::pair{"enabled", offsetof(CharacterController2D, enabled)},
          std::pair{"mode", offsetof(CharacterController2D, mode)},
          std::pair{"radius", offsetof(CharacterController2D, radius)},
          std::pair{"height", offsetof(CharacterController2D, height)},
          std::pair{"max_slope", offsetof(CharacterController2D, max_slope)},
          std::pair{"floor_probe", offsetof(CharacterController2D, floor_probe)},
          std::pair{"layer", offsetof(CharacterController2D, layer)},
          std::pair{"mask", offsetof(CharacterController2D, mask)}}) {
        const auto* member = ecs_struct_get_member(world.c_ptr(), native.id(), name);
        require(member && std::size_t(member->offset) == offset,
                "2D controller native member offset mismatch");
    }
    require(detail::canonical_builtin_value(type, Json::object()) == type.defaults,
            "2D controller defaults were not backfilled by the shared schema");
    bool mode_found = false;
    const auto schema = detail::builtin_schema();
    for (const auto& component : schema.at("components"))
        if (component.at("id") == type.name) {
            require(component.at("display_name") == "Character Controller 2D",
                    "Controller picker label");
            for (const auto& field : component.at("fields"))
                if (field.at("id") == "mode") {
                    mode_found = field.at("choices")[0].at("label") == "Grounded" &&
                                 field.at("choices")[1].at("label") == "Floating";
                }
        }
    require(mode_found, "2D controller mode choices missing");
    for (const auto& [field, value] : {std::pair<std::string, Json>{"mode", 2},
                                       {"mode", .5},
                                       {"enabled", 1},
                                       {"radius", .019},
                                       {"height", .009},
                                       {"radius", 500},
                                       {"max_slope", 85.1},
                                       {"floor_probe", -1},
                                       {"floor_probe", 1.01},
                                       {"layer", 32},
                                       {"mask", -1},
                                       {"mask", UINT64_MAX},
                                       {"max_slope", std::numeric_limits<double>::quiet_NaN()}}) {
        auto invalid = type.defaults;
        invalid[field] = value;
        rejects([&] { detail::validate_components({{type.name, invalid}}); });
    }
    require(!engine.services().available(Capability::Physics),
            "Controller reflection created a native solver");
}
struct Cleanup {
    std::filesystem::path root;
    ~Cleanup() {
        std::error_code error;
        std::filesystem::remove_all(root, error);
    }
};
void controller_authoring(EngineContext& engine, const std::filesystem::path& parent) {
    Cleanup work{parent / ("controller-" + AssetId::generate().str())};
    std::filesystem::create_directories(work.root);
    const auto& type = spec("forge.character_controller_2d");
    Scene scene(engine.world());
    const auto id = EntityId::generate().str();
    const Json opaque{{"wide", UINT64_MAX}, {"future", Json::array({nullptr, "keep"})}};
    scene.reset({{"version", 5},
                 {"asset_id", AssetId::generate()},
                 {"entities",
                  Json::array({{{"id", id},
                                {"name", "Controller source"},
                                {"spatial", {{"mode", "world"}}},
                                {"components",
                                 {{type.name, {{"mode", 1}, {"future_options", opaque}}},
                                  {"forge.local_scale", {{"x", -2}, {"y", 2}, {"z", 0}}}}}}})}});
    const auto original = scene.document();
    const auto value = scene.entity(id).get<CharacterController2D>();
    authoring_command(
        scene, "property.set",
        {{"entity", id}, {"component", type.name}, {"field", "floor_probe"}, {"value", .25}});
    require(scene.entity(id).get<CharacterController2D>().floor_probe == .25f && scene.undo() &&
                scene.document() == original && scene.redo() &&
                scene.entity(id).get<CharacterController2D>().floor_probe == .25f && scene.undo(),
            "Controller Inspector edit did not preserve typed Undo/Redo and opaque data");
    const auto definition = create_prefab_source(scene, id);
    PrefabLibrary library(work.root);
    const auto prefab = library.create(scene, definition, "Controller.prefab.json");
    const auto instance = instantiate_prefab(scene, prefab);
    const auto document = scene.document();
    bool inherited = false;
    for (const auto& row : document.at("entities"))
        if (row.at("id") == instance) {
            const auto actor = row.at("prefab_instance")
                                   .at("members")
                                   .at(definition.root().str())
                                   .get<std::string>();
            inherited = scene.entity(actor).get<CharacterController2D>() == value;
        }
    require(inherited, "Prefab realization did not inherit the controller settings");
    const auto snapshot = scene.snapshot();
    const auto path = work.root / "controller.scene.json";
    scene.save(path);
    EngineContext next(WorldRole::Authoring);
    Scene reopened(next.world());
    library.refresh(reopened);
    reopened.load(path);
    require(
        reopened.snapshot() == snapshot && reopened.reference(id) == scene.reference(id) &&
            reopened.entity(id).get<CharacterController2D>() == value &&
            reopened.entity(id).get<LocalScale>() == LocalScale{-2, 2, 0} &&
            !next.services().available(Capability::Physics),
        "Controller source/Prefab reopening lost native settings, identity, extensions or scale");
}
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
void tile_map_collider(EngineContext& engine, const std::filesystem::path& parent) {
    constexpr auto layer = UINT64_MAX - 1;
    const auto& type = spec("forge.tile_map_collider_2d");
    auto& world = engine.world().world();
    const auto native = world.lookup(type.name);
    const auto* info = ecs_get_type_info(world.c_ptr(), native.id());
    const auto* member = ecs_struct_get_member(world.c_ptr(), native.id(), "layer");
    require(info && std::size_t(info->size) == sizeof(TileMapCollider2D) &&
                std::size_t(info->alignment) == alignof(TileMapCollider2D) && member &&
                member->offset == offsetof(TileMapCollider2D, layer) &&
                member->type == world.id<std::uint64_t>(),
            "Tile Map Collider 2D lost its actual native uint64 layout");
    auto target = world.entity();
    target.set<TileMapCollider2D>({layer});
    detail::validate_reflected_value(target, target.get<TileMapCollider2D>());
    require(type.read(target, false) == Json{{"layer", layer}} &&
                std::get<TileMapCollider2D>(type.decode(Json{{"layer", layer}})).layer == layer,
            "Tile Map layer key passed through float or a row index");
    for (const Json invalid : {Json(-1), Json(1.5), Json(true), Json("1")})
        rejects([&] { (void)type.decode({{"layer", invalid}}); });
    target.destruct();
    const auto bounded = world.component<BoundedLayerKey>("fixture.BoundedLayerKey");
    ecs_struct_desc_t descriptor{};
    descriptor.entity = bounded.id();
    auto& field = descriptor.members[0];
    field.name = "value";
    field.type = world.id<std::uint64_t>();
    field.offset = offsetof(BoundedLayerKey, value);
    field.use_offset = true;
    constexpr auto upper = std::uint64_t{1} << 63;
    field.range = {0, double(upper)};
    require(ecs_struct_init(world.c_ptr(), &descriptor) == bounded.id(),
            "Native bounded uint64 fixture registration failed");
    auto numeric = world.entity();
    detail::validate_reflected_value(numeric, BoundedLayerKey{upper});
    // upper+1 rounds to upper in double. Admission must compare the integer.
    rejects([&] { detail::validate_reflected_value(numeric, BoundedLayerKey{upper + 1}); });
    numeric.destruct();
    Cleanup work{parent / ("tile-collider-" + AssetId::generate().str())};
    std::filesystem::create_directories(work.root);
    Scene scene(engine.world());
    const auto id = EntityId::generate().str();
    const auto map = AssetId::generate();
    auto renderer = spec("forge.tile_map_renderer").defaults;
    renderer["map"] = map;
    const Json opaque{{"wide", UINT64_MAX}, {"preserve", Json::array({nullptr, "future"})}};
    scene.reset({{"version", 5},
                 {"asset_id", AssetId::generate()},
                 {"entities", Json::array({{{"id", id},
                                            {"name", "Map collision source"},
                                            {"components",
                                             {{"forge.tile_map_renderer", renderer},
                                              {"forge.physics_body_2d", Json::object()},
                                              {"forge.tile_map_collider_2d",
                                               {{"layer", layer}, {"future", opaque}}}}}}})}});
    const auto original = scene.document();
    authoring_command(
        scene, "property.set",
        {{"entity", id}, {"component", type.name}, {"field", "layer"}, {"value", 1u}});
    require(scene.entity(id).get<TileMapCollider2D>().layer == 1 && scene.undo() &&
                scene.document() == original && scene.redo() &&
                scene.entity(id).get<TileMapCollider2D>().layer == 1 && scene.undo(),
            "Tile Map layer property did not use exact typed history");
    const auto duplicate = scene.duplicate_subtree(id);
    require(scene.entity(duplicate).get<TileMapCollider2D>().layer == layer &&
                scene.entity(duplicate).get<TileMapRenderer>().map.id == map && scene.undo() &&
                scene.document() == original,
            "Map duplication remapped a layer key or changed the shared asset reference");
    const auto definition = create_prefab_source(scene, id);
    PrefabLibrary library(work.root);
    const auto asset = library.create(scene, definition, "MapCollider.prefab.json");
    const auto first = instantiate_prefab(scene, asset), second = instantiate_prefab(scene, asset);
    for (const auto& instance : {first, second}) {
        const auto document = scene.document();
        for (const auto& row : document.at("entities"))
            if (row.at("id") == instance) {
                const auto actor = row.at("prefab_instance")
                                       .at("members")
                                       .at(definition.root().str())
                                       .get<std::string>();
                require(scene.entity(actor).get<TileMapCollider2D>().layer == layer &&
                            scene.entity(actor).get<TileMapRenderer>().map.id == map,
                        "Prefab realization changed Map-local keys or external asset identity");
            }
    }
    const auto snapshot = scene.snapshot();
    const auto path = work.root / "map-collider.scene.json";
    scene.save(path);
    EngineContext reopened_engine(WorldRole::Authoring);
    Scene reopened(reopened_engine.world());
    library.refresh(reopened);
    reopened.load(path);
    require(reopened.snapshot() == snapshot &&
                reopened.entity(id).get<TileMapCollider2D>().layer == layer &&
                !reopened_engine.services().available(Capability::Physics),
            "Map collider source/Prefab reopening lost opaque data or created an authoring solver");
}
} // namespace
int main(int argc, char** argv) {
    try {
        require(argc == 2, "Expected a private schema fixture directory");
        EngineContext engine(WorldRole::Authoring);
        native_layout(engine);
        reflected_units(engine);
        scalar_admission(engine);
        std::cout << "Checking2D controller schema\n";
        controller_schema(engine);
        std::cout << "Checking2D controller authoring\n";
        controller_authoring(engine, argv[1]);
        std::cout << "2D controller schema/authoring PASS\n";
        source_round_trip(engine, argv[1]);
        tile_map_collider(engine, argv[1]);
        std::cout << "2D authored layout, scalar rejection, source/identity/extension round trip "
                     "passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
