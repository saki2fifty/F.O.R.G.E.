#pragma once
#include <algorithm>
void prepare_joint_collision(Fixture& f, bool collide = true) {
    f.setup();
    auto config = PhysicsConfig{};
    config.layers[31] = "Connected bodies";
    f.physics->configure(config);
    for (const auto* name : {"a", "b"}) {
        auto body = f.entity(name).get<PhysicsBody>();
        body.gravity_factor = 0;
        body.linear_damping = body.angular_damping = 0;
        body.layer = 31;
        body.mask = (1u << 31) | 1u;
        f.entity(name).set<PhysicsBody>(body);
        f.entity(name).set<BoxCollider>({1, 1, 1});
        f.entity(name).set<LocalTranslation>({name[0] == 'a' ? 0. : .9, 0, 0});
    }
    auto joint = f.entity("j").get<PhysicsJoint>();
    joint.min_distance = .001f;
    joint.max_distance = 10;
    joint.collide_connected = collide;
    f.entity("j").set<PhysicsJoint>(joint);
}
bool joint_pair_event(Fixture& f, const char* first, const char* second, bool begin) {
    const auto a = f.ref(first), b = f.ref(second);
    for (const auto& event : f.physics->contacts())
        if (event.begin == begin &&
            ((event.first == a && event.second == b) || (event.first == b && event.second == a)))
            return true;
    return false;
}
void joint_collision_policy() {
    auto legacy = physics_detail::joint_value(PhysicsJoint{});
    legacy.erase("collide_connected");
    require(physics_detail::joint_from_value(legacy).collide_connected,
            "Legacy joint default disabled existing contacts");

    Fixture cached;
    prepare_joint_collision(cached);
    cached.tick();
    require(joint_pair_event(cached, "a", "b", true) && point(cached, "b")[0] > .9,
            "Enabled connected bodies lost solid response or full32-bit masks");
    auto joint = cached.entity("j").get<PhysicsJoint>();
    joint.collide_connected = false;
    const auto before = point(cached, "b");
    cached.entity("j").set<PhysicsJoint>(joint);
    cached.tick();
    require(distance(point(cached, "b"), before) < 1e-6 &&
                joint_pair_event(cached, "a", "b", false),
            "Connected opt-out reused a cached contact or lost its End");
    const auto excluded = cached.physics->checkpoint();
    auto invalid = joint;
    invalid.body_b = invalid.body_a;
    cached.entity("j").set<PhysicsJoint>(invalid);
    rejects([&] { cached.physics->synchronize(0); });
    cached.entity("j").set<PhysicsJoint>(joint);
    require(cached.physics->checkpoint() == excluded,
            "Rejected pair candidate changed admitted contact/constraint state");
    Fixture recovered;
    recovered.scene.restore_snapshot(cached.scene.snapshot());
    auto config = PhysicsConfig{};
    config.layers[31] = "Connected bodies";
    recovered.physics->configure(config);
    recovered.physics->restore(excluded);
    cached.tick();
    recovered.tick();
    require(distance(point(cached, "b"), point(recovered, "b")) < 1e-6 &&
                !joint_pair_event(recovered, "a", "b", true),
            "Fresh recovery lost derived collision policy");
    PhysicsOverlap query;
    query.origin = {.4, 0, 0};
    query.dimensions = {2, 0, 0};
    const auto overlap = cached.physics->overlap_shape(query, {1u << 31, false});
    require(overlap.size() == 2 &&
                std::find(overlap.begin(), overlap.end(), cached.ref("a")) != overlap.end() &&
                std::find(overlap.begin(), overlap.end(), cached.ref("b")) != overlap.end(),
            "Pair exclusion hid realized geometry from overlap queries");
    const auto ray = cached.physics->raycast_filtered({2, 0, 0}, {-3, 0, 0}, {1u << 31, false});
    require(ray && ray->entity == cached.ref("b"), "Pair exclusion hid a ray target");
    joint.collide_connected = true;
    cached.entity("j").set<PhysicsJoint>(joint);
    recovered.entity("j").set<PhysicsJoint>(joint);
    cached.tick();
    recovered.tick();
    require(point(cached, "b")[0] > before[0] + .005 && joint_pair_event(cached, "a", "b", true) &&
                distance(point(cached, "b"), point(recovered, "b")) < 1e-6,
            "Re-enabled cached pair did not restore solid response/recovery");

    // Any enabled opt-out wins; reversing the refs does not change pair identity.
    Fixture duplicates;
    prepare_joint_collision(duplicates, false);
    joint = duplicates.entity("j").get<PhysicsJoint>();
    auto reverse = joint;
    std::swap(reverse.body_a, reverse.body_b);
    reverse.collide_connected = true;
    duplicates.entity("j2").set<PhysicsJoint>(reverse);
    duplicates.tick();
    const auto order = duplicates.physics->checkpoint().at("joints");
    require(std::abs(point(duplicates, "b")[0] - .9) < 1e-6,
            "Permissive duplicate overrode the opt-out");
    reverse.collide_connected = false;
    duplicates.entity("j2").set<PhysicsJoint>(reverse);
    duplicates.tick();
    joint.collide_connected = true;
    duplicates.entity("j").set<PhysicsJoint>(joint);
    duplicates.tick();
    const auto retained = duplicates.physics->checkpoint().at("joints");
    require(retained.size() == order.size() && retained[0].at("entity") == order[0].at("entity") &&
                retained[1].at("entity") == order[1].at("entity") &&
                std::abs(point(duplicates, "b")[0] - .9) < 1e-6,
            "Pair-only edits rebuilt native constraints or ignored reverse opt-out");
    reverse.enabled = false;
    duplicates.entity("j2").set<PhysicsJoint>(reverse);
    duplicates.tick();
    require(joint_pair_event(duplicates, "a", "b", true),
            "Disabled opt-out still suppressed a connected pair");

    Fixture sleeping;
    prepare_joint_collision(sleeping, false);
    sleeping.tick(90);
    require(!sleeping.physics->body_state(sleeping.ref("b"))->active,
            "Excluded resting body did not sleep");
    sleeping.physics->synchronize(0);
    require(!sleeping.physics->body_state(sleeping.ref("b"))->active,
            "Unchanged pair policy woke a sleeping body");
    auto tune = sleeping.entity("j").get<PhysicsJoint>();
    tune.collide_connected = true;
    sleeping.entity("j").set<PhysicsJoint>(tune);
    sleeping.physics->synchronize(0);
    require(sleeping.physics->body_state(sleeping.ref("b"))->active,
            "Re-enabled pair failed to wake its sleeping endpoint");
    sleeping.tick();
    require(joint_pair_event(sleeping, "a", "b", true),
            "Sleeping pair did not restart contact detection");

    Fixture replacement;
    prepare_joint_collision(replacement, false);
    replacement.tick();
    const auto native_body = [](Fixture& f, const EntityRef& ref) {
        const auto checkpoint = f.physics->checkpoint();
        for (const auto& row : checkpoint.at("mapping"))
            if (row.at("entity").get<EntityRef>() == ref)
                return row.at("body").get<std::uint32_t>();
        throw std::runtime_error("Missing native body mapping");
    };
    const auto old_body = native_body(replacement, replacement.ref("b"));
    auto heavier = replacement.entity("b").get<PhysicsBody>();
    heavier.mass = 2;
    replacement.entity("b").set<PhysicsBody>(heavier);
    replacement.tick();
    require(native_body(replacement, replacement.ref("b")) != old_body &&
                std::abs(point(replacement, "b")[0] - .9) < 1e-6 &&
                !joint_pair_event(replacement, "a", "b", true),
            "Native body replacement lost current pair-filter ownership");
    replacement.entity("j").remove<PhysicsJoint>();
    replacement.tick();
    require(joint_pair_event(replacement, "a", "b", true),
            "Retired joint left a stale pair exclusion");

    Fixture generation;
    prepare_joint_collision(generation, false);
    generation.tick();
    const auto old_entity = generation.entity("b").id();
    const auto durable_ref = generation.ref("b");
    const auto old_native = native_body(generation, durable_ref);
    const auto original = generation.scene.snapshot();
    auto removed = original;
    auto& entities = removed.at("entities");
    entities.erase(std::remove_if(entities.begin(), entities.end(),
                                  [&](const auto& row) {
                                      return row.at("id").template get<EntityId>() ==
                                             durable_ref.entity;
                                  }),
                   entities.end());
    for (auto& row : entities)
        row.at("components").erase("forge.physics_joint");
    generation.scene.restore_snapshot(removed);
    generation.physics->synchronize(0);
    generation.scene.restore_snapshot(original);
    require(generation.entity("b").id() != old_entity && generation.ref("b") == durable_ref,
            "Fixture did not replace the entity generation under its original UUID");
    auto recreated = generation.entity("j").get<PhysicsJoint>();
    recreated.collide_connected = true;
    generation.entity("j").set<PhysicsJoint>(recreated);
    generation.tick();
    require(native_body(generation, durable_ref) != old_native &&
                joint_pair_event(generation, "a", "b", true),
            "Retired generation left a stale pair exclusion on its replacement");
    recreated.collide_connected = false;
    const auto recreated_position = point(generation, "b");
    generation.entity("j").set<PhysicsJoint>(recreated);
    generation.tick();
    require(distance(point(generation, "b"), recreated_position) < 1e-6 &&
                joint_pair_event(generation, "a", "b", false),
            "New entity generation did not receive its current pair policy");

    Fixture floor;
    prepare_joint_collision(floor, false);
    PhysicsBody ground;
    ground.mask = 1u << 31;
    floor.entity("j3").set<PhysicsBody>(ground);
    floor.entity("j3").set<BoxCollider>({6, 1, 6});
    floor.entity("j3").set<LocalTranslation>({0, -.9, 0});
    auto falling = floor.entity("b").get<PhysicsBody>();
    falling.gravity_factor = 1;
    floor.entity("b").set<PhysicsBody>(falling);
    floor.tick();
    require(!joint_pair_event(floor, "a", "b", true) && joint_pair_event(floor, "b", "j3", true),
            "Joint pair opt-out suppressed unrelated floor collision");
    floor.tick(180);
    // Ideal support is -.9 + .5 + .5 = .1m. The exact pinned default
    // penetration slop is .02m; resting native contacts settle at .08m.
    if (!(std::abs(point(floor, "b")[1] - .08) < 1e-4))
        throw std::runtime_error(
            "Connected-body floor result: position=" + Json(point(floor, "b")).dump() + " floor=" +
            Json(point(floor, "j3")).dump() + " state=" + floor.physics->status().dump());
    ground.mask = 0;
    floor.entity("j3").set<PhysicsBody>(ground);
    floor.tick(30);
    require(point(floor, "b")[1] < -.5, "Pair policy overrode the other body's zero mask");

    // Early group filtering is also consulted by native continuous collision.
    for (bool collide : {false, true}) {
        Fixture ccd;
        prepare_joint_collision(ccd, collide);
        ccd.entity("b").set<LocalTranslation>({-2, 0, 0});
        auto body = ccd.entity("b").get<PhysicsBody>();
        body.continuous_collision = true;
        ccd.entity("b").set<PhysicsBody>(body);
        ccd.physics->set_linear_velocity(ccd.ref("b"), {240, 0, 0});
        ccd.tick();
        require(collide ? point(ccd, "b")[0] < -.95 : point(ccd, "b")[0] > 1.9,
                "CCD ignored the connected-body collision policy");
    }
}
