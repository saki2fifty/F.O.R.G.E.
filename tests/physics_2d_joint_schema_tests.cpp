#include "builtins.hpp"
#include <cstddef>
#include <filesystem>
#include <forge/scene.hpp>
#include <iostream>
#include <stdexcept>

using namespace forge;
namespace {
void require(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
template <class Operation> void rejects(Operation&& operation) {
    try {
        operation();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error("Invalid 2D joint source was accepted");
}
void layout_and_metadata(EngineContext& engine) {
    auto& world = engine.world().world();
    const auto component = world.lookup("forge.physics_joint_2d");
    require(component && component == world.component<PhysicsJoint2D>(),
            "2D joint is not in the existing physics schema family");
    require(component != world.component<PhysicsJoint>(), "2D reinterpreted the 3D joint type");
    const auto* layout = ecs_get(world.c_ptr(), component.id(), EcsComponent);
    require(layout && layout->size == sizeof(PhysicsJoint2D) &&
                layout->alignment == alignof(PhysicsJoint2D),
            "Native 2D joint layout mismatch");
    for (const auto [name, offset] :
         {std::pair{"enabled", offsetof(PhysicsJoint2D, enabled)},
          std::pair{"kind", offsetof(PhysicsJoint2D, kind)},
          std::pair{"body_a", offsetof(PhysicsJoint2D, body_a)},
          std::pair{"body_b", offsetof(PhysicsJoint2D, body_b)},
          std::pair{"anchor_a_x", offsetof(PhysicsJoint2D, anchor_a_x)},
          std::pair{"anchor_a_y", offsetof(PhysicsJoint2D, anchor_a_y)},
          std::pair{"anchor_b_x", offsetof(PhysicsJoint2D, anchor_b_x)},
          std::pair{"anchor_b_y", offsetof(PhysicsJoint2D, anchor_b_y)},
          std::pair{"frame_a_angle", offsetof(PhysicsJoint2D, frame_a_angle)},
          std::pair{"frame_b_angle", offsetof(PhysicsJoint2D, frame_b_angle)},
          std::pair{"distance", offsetof(PhysicsJoint2D, distance)},
          std::pair{"spring_frequency", offsetof(PhysicsJoint2D, spring_frequency)},
          std::pair{"spring_damping", offsetof(PhysicsJoint2D, spring_damping)},
          std::pair{"limits_enabled", offsetof(PhysicsJoint2D, limits_enabled)},
          std::pair{"min_angle", offsetof(PhysicsJoint2D, min_angle)},
          std::pair{"max_angle", offsetof(PhysicsJoint2D, max_angle)},
          std::pair{"min_travel", offsetof(PhysicsJoint2D, min_travel)},
          std::pair{"max_travel", offsetof(PhysicsJoint2D, max_travel)},
          std::pair{"motor_enabled", offsetof(PhysicsJoint2D, motor_enabled)},
          std::pair{"hinge_motor_speed", offsetof(PhysicsJoint2D, hinge_motor_speed)},
          std::pair{"slider_motor_speed", offsetof(PhysicsJoint2D, slider_motor_speed)},
          std::pair{"max_motor_torque", offsetof(PhysicsJoint2D, max_motor_torque)},
          std::pair{"max_motor_force", offsetof(PhysicsJoint2D, max_motor_force)},
          std::pair{"collide_connected", offsetof(PhysicsJoint2D, collide_connected)}}) {
        const auto* member = ecs_struct_get_member(world.c_ptr(), component.id(), name);
        require(member && std::size_t(member->offset) == offset, "2D joint field offset mismatch");
    }
    const auto schema = detail::builtin_schema();
    bool found = false;
    for (const auto& type : schema.at("components"))
        if (type.at("id") == "forge.physics_joint_2d") {
            found = true;
            require(type.at("display_name") == "Physics Joint 2D", "Joint picker label mismatch");
            for (const auto& field : type.at("fields")) {
                const auto id = field.at("id").get<std::string>();
                if (id == "kind") {
                    const auto& options = field.at("choices");
                    require(options.size() == 3 && options[0].at("label") == "Distance" &&
                                options[1].at("label") == "Hinge" &&
                                options[2].at("label") == "Slider",
                            "Joint kind choices reinterpret another domain's IDs");
                }
                if (id == "distance" || id == "max_motor_force" || id == "hinge_motor_speed" ||
                    id == "spring_frequency") {
                    const auto native_member = component.lookup(id.c_str());
                    require(native_member.has<EcsMember>() && native_member.get<EcsMember>().unit,
                            "Joint physical field has no actual native unit");
                    const auto* unit =
                        ecs_get(world.c_ptr(), native_member.get<EcsMember>().unit, EcsUnit);
                    require(unit && unit->symbol && field.at("unit") == unit->symbol,
                            "Joint Inspector unit disagrees with registered unit");
                }
                if (id == "body_a" || id == "body_b")
                    require(field.at("type") == "entity_ref",
                            "Joint lost typed EntityRef metadata");
            }
        }
    require(found, "Missing 2D joint schema projection");
    require(!engine.services().available(Capability::Physics), "Joint schema created a solver");
}
void source_round_trip(EngineContext& engine, const std::filesystem::path& root) {
    const auto directory = root / AssetId::generate().str();
    std::filesystem::create_directories(directory);
    Scene scene(engine.world());
    const auto a = EntityId::generate().str(), b = EntityId::generate().str(),
               j = EntityId::generate().str();
    auto rows = Json::array();
    for (const auto& [id, name] :
         {std::pair{a, "Anchor"}, std::pair{b, "Body"}, std::pair{j, "Joint"}})
        rows.push_back({{"id", id}, {"name", name}, {"components", Json::object()}});
    scene.reset({{"version", 5}, {"asset_id", AssetId::generate()}, {"entities", rows}});
    PhysicsJoint2D value;
    value.kind = 2;
    value.body_a = scene.reference(a);
    value.body_b = scene.reference(b);
    value.anchor_a_x = -2;
    value.frame_a_angle = 90;
    value.motor_enabled = true;
    value.slider_motor_speed = -.25f;
    scene.entity(j).set<PhysicsJoint2D>(value);
    PhysicsJoint old;
    old.enabled = false;
    old.kind = 4; // 3D Slider kind must retain its existing meaning and bytes.
    scene.entity(j).set<PhysicsJoint>(old);
    auto authored = scene.snapshot();
    for (auto& entity : authored.at("entities"))
        if (entity.at("id") == j)
            entity["components"]["forge.physics_joint_2d"]["future"] =
                Json{{"opaque", UINT64_C(18446744073709551614)}};
    scene.reset(authored);
    const auto before = scene.snapshot();
    const auto path = directory / "joints.scene.json";
    scene.save(path);
    EngineContext next(WorldRole::Authoring);
    Scene reopened(next.world());
    reopened.load(path);
    require(reopened.snapshot() == before, "Joint reopening lost identity/extensions or 3D source");
    require(reopened.entity(j).get<PhysicsJoint2D>() == value &&
                reopened.entity(j).get<PhysicsJoint>() == old,
            "Joint typed reopening changed a domain's configuration");
    auto reject_field = [&](const char* field, Json invalid) {
        auto candidate = before;
        for (auto& entity : candidate.at("entities"))
            if (entity.at("id") == j)
                entity["components"]["forge.physics_joint_2d"][field] = std::move(invalid);
        rejects([&] { reopened.reset(candidate); });
        require(reopened.snapshot() == before, "Rejected joint source changed the admitted scene");
    };
    reject_field("kind", 3);
    reject_field("kind", -1);
    reject_field("distance", 0);
    reject_field("distance", .0049);
    reject_field("frame_a_angle", 181);
    reject_field("max_angle", 179);
    reject_field("min_angle", 100);
    reject_field("max_travel", -2);
    reject_field("spring_frequency", .001);
    reject_field("spring_damping", -1);
    reject_field("max_motor_force", -1);
    reject_field("slider_motor_speed", 1001);
    reject_field("body_a", Json{{"scene", value.body_a.scene}, {"entity", nullptr}});
    require(!next.services().available(Capability::Physics),
            "Joint source reopening created a solver");
    std::filesystem::remove_all(directory);
}
} // namespace
int main(int argc, char** argv) {
    try {
        require(argc == 2, "Expected private joint schema fixture directory");
        EngineContext engine(WorldRole::Authoring);
        layout_and_metadata(engine);
        source_round_trip(engine, argv[1]);
        std::cout << "2D joint native layout/units/references, source round trip, extension/3D "
                     "preservation and atomic invalid-source rejection passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
