#include "physics_2d.hpp"
#include <cmath>
#include <forge/character_components.hpp>
#include <forge/scene.hpp>
#include <iostream>
#include <numbers>

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
    throw std::runtime_error("Unsupported 2D body/ancestry was accepted");
}
struct Fixture {
    EngineContext engine{WorldRole::Authoring};
    Scene scene{engine.world()};
    std::string parent = EntityId::generate().str(), child = EntityId::generate().str();
    Fixture() {
        const Json components = {{"forge.local_translation", {{"x", 0}, {"y", 0}, {"z", 0}}}};
        scene.reset({{"version", 5},
                     {"asset_id", AssetId::generate()},
                     {"entities",
                      Json::array({{{"id", parent}, {"name", "Plane"}, {"components", components}},
                                   {{"id", child},
                                    {"name", "Body"},
                                    {"parent", parent},
                                    {"spatial", {{"mode", "follow_structure"}}},
                                    {"components", components}}})}});
        entity().set<PhysicsBody2D>({});
        entity().set<BoxCollider2D>({});
    }
    flecs::entity entity() { return scene.entity(child); }
    flecs::entity ancestor() { return scene.entity(parent); }
    physics_detail::Desired2D desired() {
        return physics_detail::desired_bodies_2d(engine.world(), {},
                                                 engine.world().transform_nodes());
    }
};
void signed_parent_frames() {
    Fixture test;
    unsigned cases = 0;
    for (float px : {-2.f, 2.f})
        for (float py : {-2.f, 2.f})
            for (float pz : {-3.f, 0.f, 3.f})
                for (float cx : {-1.f, 1.f})
                    for (float cy : {-2.f, 2.f})
                        for (float cz : {-1.f, 0.f, 1.f}) {
                            test.ancestor().set<LocalScale>({px, py, pz});
                            test.ancestor().set<LocalRotation>(rotation_about_axis({0, 0, 1}, 37));
                            test.ancestor().set<LocalTranslation>({999990, -999990, 11});
                            test.entity().set<LocalScale>({cx, cy, cz});
                            test.entity().set<LocalRotation>(rotation_about_axis({0, 0, 1}, -23));
                            test.entity().set<LocalTranslation>({1, -1, 2});
                            const auto nodes = test.engine.world().transform_nodes();
                            const auto expected =
                                evaluate_transforms(nodes).at(test.entity().id()).affine;
                            const auto before = test.scene.snapshot();
                            const auto admitted = test.desired().at(test.entity().id());
                            const auto actual = affine_transform(admitted.pose);
                            for (unsigned i = 0; i < 12; ++i)
                                require(std::abs(actual.m[i] - expected.m[i]) < 2e-5,
                                        "Planar admission changed the actual signed parent frame");
                            require(std::abs(admitted.pose.rotation.x) < 1e-6 &&
                                        std::abs(admitted.pose.rotation.y) < 1e-6 &&
                                        admitted.config.dimensions == std::array<float, 2>{2, 4},
                                    "Signed depth selected a tilted orientation or wrong geometry");
                            require(test.scene.snapshot() == before,
                                    "Detached 2D admission wrote authored data");
                            ++cases;
                        }
    require(cases == 144, "Signed parent coverage changed");
}
void invalid_frames_and_writers() {
    Fixture test;
    test.ancestor().set<LocalScale>({2, 3, 0});
    test.entity().set<LocalRotation>(rotation_about_axis({0, 0, 1}, 30));
    rejects([&] { test.desired(); }); // Parent-induced XY shear.
    test.entity().set<LocalRotation>({});
    test.ancestor().set<LocalScale>({1, 1, 0});
    test.ancestor().set<LocalRotation>(rotation_about_axis({1, 0, 0}, 180));
    rejects([&] { test.desired(); }); // An untilted-looking flat affine cannot hide X rotation.
    test.ancestor().set<LocalRotation>({});
    test.ancestor().set<LocalScale>({0, 1, 0});
    rejects([&] { test.desired(); });
    test.ancestor().set<LocalScale>({1e-9f, 1, 0});
    rejects([&] { test.desired(); });
    test.ancestor().set<LocalScale>({1, 1, 0});
    test.ancestor().set<PhysicsBody>({2});
    rejects([&] { test.desired(); });
    test.ancestor().remove<PhysicsBody>();
    test.ancestor().set<PhysicsBody2D>({2});
    rejects([&] { test.desired(); });
    test.ancestor().remove<PhysicsBody2D>();
    test.ancestor().set<CharacterController>({});
    rejects([&] { test.desired(); });
    test.ancestor().remove<CharacterController>();
    test.entity().set<PhysicsBody2D>({2});
    rejects([&] { test.desired(); }); // A Dynamic body needs an explicit World boundary.
    test.entity().set<SpatialBinding>({SpatialMode::World});
    require(test.desired().size() == 1, "World binding did not separate Dynamic 2D ancestry");
    test.entity().set<PhysicsBody>({});
    rejects([&] { test.desired(); });
    auto disabled = PhysicsBody{};
    disabled.enabled = false;
    test.entity().set<PhysicsBody>(disabled);
    require(test.desired().size() == 1, "Disabled 3D choice conflicted with 2D writer");
    test.entity().set<CharacterController>({});
    rejects([&] { test.desired(); });
}
void effective_profile() {
    Fixture test;
    test.entity().set<BoxCollider2D>({.01f, 1000});
    require(test.desired().at(test.entity().id()).config.dimensions[0] == .01f,
            "Representable float minimum box dimension was rejected");
    test.entity().set<BoxCollider2D>({.009f, 1});
    rejects([&] { test.desired(); });
    test.entity().remove<BoxCollider2D>();
    test.entity().set<CircleCollider2D>({.005f});
    require(test.desired().size() == 1, "Representable minimum circle radius was rejected");
    test.entity().set<LocalScale>({1, 2, 0});
    rejects([&] { test.desired(); });
    test.entity().set<LocalScale>({-1, 1, 0});
    test.entity().remove<CircleCollider2D>();
    test.entity().set<CapsuleCollider2D>({.005f, .01f});
    require(test.desired().size() == 1, "Representable minimum capsule was rejected");
    test.entity().set<CapsuleCollider2D>({.25f, .005f});
    rejects([&] { test.desired(); });
    test.entity().set<CapsuleCollider2D>({500, 1});
    rejects([&] { test.desired(); });
    test.entity().set<CapsuleCollider2D>({.25f, 1});
    test.entity().set<LocalTranslation>({1000000.01, 0, 0});
    rejects([&] { test.desired(); });
    test.entity().set<LocalTranslation>({1000000, -1000000, 7});
    require(test.desired().size() == 1, "Supported far-coordinate input was rejected");
    require(!test.engine.services().available(Capability::Physics),
            "Body admission created an authoring solver");
}
void controller_profile_and_writers() {
    Fixture test;
    test.entity().remove<PhysicsBody2D>();
    test.entity().set<CharacterController2D>({});
    rejects([&] { test.desired(); });
    test.entity().set<SpatialBinding>({SpatialMode::World});
    test.entity().set<LocalScale>({-2, 2, 0});
    test.entity().set<LocalTranslation>({999990, -999990, 7});
    const auto desired = test.desired();
    const auto& actor = desired.at(test.entity().id());
    require(actor.config.mover_profile && actor.config.body.motion == 1 &&
                actor.config.dimensions == std::array<float, 2>{.6f, 1.2f} &&
                std::abs(actor.config.mover_profile->minimum_floor_y -
                         std::cos(50 * std::numbers::pi / 180)) < 1e-6 &&
                actor.config.mover_profile->floor_probe == .1f && actor.pose.translation.z == 7 &&
                !test.entity().has<PhysicsBody2D>(),
            "Authored controller did not produce one derived feet-origin native role");
    test.entity().set<LocalRotation>(rotation_about_axis({0, 0, 1}, 10));
    rejects([&] { test.desired(); });
    test.entity().set<LocalRotation>({});
    for (auto scale : {LocalScale{2, 3, 0}, LocalScale{-2, -2, 0}, LocalScale{.01f, .01f, 0}}) {
        test.entity().set<LocalScale>(scale);
        rejects([&] { test.desired(); });
    }
    test.entity().set<LocalScale>({1, 1, 1});
    test.entity().set<LocalTranslation>({999990, 1000000, 7});
    rejects([&] { test.desired(); }); // Feet fit, capsule query center does not.
    test.entity().set<LocalTranslation>({999990, -999990, 7});
    test.entity().set<PhysicsBody2D>({});
    rejects([&] { test.desired(); });
    test.entity().remove<PhysicsBody2D>();
    test.entity().set<PhysicsBody>({});
    rejects([&] { test.desired(); });
    test.entity().remove<PhysicsBody>();
    test.entity().set<CharacterController>({});
    rejects([&] { test.desired(); });
    test.entity().remove<CharacterController>();
    auto floating = CharacterController2D{};
    floating.mode = 1;
    test.entity().set<CharacterController2D>(floating);
    require(test.desired().at(test.entity().id()).config.mover_profile->minimum_floor_y == -1,
            "Floating authoring did not select native floating motion");
    Fixture ancestry;
    ancestry.ancestor().set<SpatialBinding>({SpatialMode::World});
    ancestry.ancestor().set<CharacterController2D>({});
    rejects([&] { ancestry.desired(); });
    ancestry.entity().set<SpatialBinding>({SpatialMode::World});
    require(ancestry.desired().size() == 2, "World binding did not separate a controller ancestor");
    require(!test.engine.services().available(Capability::Physics),
            "Controller admission created an authoring solver");
}
} // namespace
int main() {
    try {
        signed_parent_frames();
        invalid_frames_and_writers();
        effective_profile();
        controller_profile_and_writers();
        std::cout
            << "2D detached signed-frame, ancestry/writer and effective profile admission passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
