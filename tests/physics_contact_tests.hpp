#pragma once
#include <limits>
#include <thread>

Json moving_sensor_scene() {
    auto barrier = body("barrier", 0, 0), actor = body("actor", 0, 2);
    actor["components"]["forge.position"]["x"] = -3;
    auto& settings = actor["components"]["forge.physics_body"];
    settings["mass"] = 1;
    settings["sensor"] = true;
    settings["gravity_factor"] = 0;
    settings["linear_damping"] = 0;
    settings["angular_damping"] = 0;
    settings["allow_sleeping"] = false;
    return {{"version", 1}, {"entities", Json::array({barrier, actor})}};
}
bool same_contact(const PhysicsContact& a, const PhysicsContact& b) {
    return a.first == b.first && a.second == b.second && a.begin == b.begin && a.tick == b.tick;
}
void physics_contact_tests() {
    {
        Fixture f;
        f.load(moving_sensor_scene());
        const auto actor = *f.engine.world().reference(f.scene.entity("actor").id());
        const auto barrier = *f.engine.world().reference(f.scene.entity("barrier").id());
        f.physics->set_linear_velocity(actor, {2, 0, 0});
        unsigned begins = 0, ends = 0;
        std::uint64_t begin_tick = 0, end_tick = 0;
        for (unsigned tick = 1; tick <= 180; ++tick) {
            f.tick();
            check(std::abs(f.scene.entity("actor").get<LocalTranslation>().x -
                           (-3 + 2 * double(1.f / 60) * tick)) < 1e-5,
                  "Moving sensor received solid collision response");
            const auto count = f.physics->contacts().size();
            for (std::size_t index = 0; index < count; ++index) {
                const auto event = f.physics->contact_event(index);
                const auto again = f.physics->contact_event(index);
                check(event && again && same_contact(*event, *again) && event->tick == tick &&
                          ((event->first == actor && event->second == barrier) ||
                           (event->first == barrier && event->second == actor)),
                      "Copied contacts lost canonical pair/tick or consumed broadcast slots");
                if (event->begin) {
                    ++begins;
                    begin_tick = event->tick;
                } else {
                    ++ends;
                    end_tick = event->tick;
                }
                if (begins == 1 && event->begin) {
                    const auto before = f.physics->checkpoint();
                    bool rejected = false;
                    std::thread foreign([&] {
                        try {
                            f.physics->contact_event(index);
                        } catch (const std::exception&) {
                            rejected = true;
                        }
                    });
                    foreign.join();
                    check(rejected && f.physics->checkpoint() == before,
                          "Contact observation accepted foreign owner or mutated recovery state");
                }
            }
            check(!f.physics->contact_event(count) &&
                      !f.physics->contact_event(std::numeric_limits<std::size_t>::max()) &&
                      f.physics->contacts().size() == count,
                  "Contact index overflow fabricated data or changed slot count");
        }
        // Native speculative/cache timing is not promised as geometric overlap.
        check(begins == 1 && ends == 1 && begin_tick >= 55 && begin_tick <= 75 && end_tick >= 115 &&
                  end_tick <= 135,
              "Moving sensor failed controlled native cache Begin/End sequence");
        f.physics->stop();
        reject([&] { f.physics->contact_event(0); });
    }
    {
        Fixture f;
        f.load(source());
        const auto cube = *f.engine.world().reference(f.scene.entity("cube").id());
        bool began = false, sleeping_end = false;
        for (unsigned tick = 1; tick <= 360; ++tick) {
            f.tick();
            for (std::size_t index = 0; index < f.physics->contacts().size(); ++index) {
                const auto event = f.physics->contact_event(index);
                check(event && event->tick == tick,
                      "Solid contact slot failed copied owner/tick admission");
                began |= event->begin;
                if (!event->begin && !f.physics->body_state(cube)->active)
                    sleeping_end = true;
            }
        }
        // Selected Jolt's 0.02 m penetration slop permits the cube to rest at
        // y=0.48 while its half-height is 0.5. Sleep removal is still no exit.
        if (!(began && sleeping_end && y(f) > .47 && y(f) < .51))
            throw std::runtime_error(
                "Sleeping solid contact evidence: began=" + std::to_string(began) +
                " sleeping_end=" + std::to_string(sleeping_end) + " y=" + std::to_string(y(f)) +
                " active=" + std::to_string(f.physics->body_state(cube)->active));
    }
    {
        Fixture f;
        f.load(moving_sensor_scene());
        const auto actor = *f.engine.world().reference(f.scene.entity("actor").id());
        f.physics->set_linear_velocity(actor, {2, 0, 0});
        for (unsigned i = 0; i < 90 && f.physics->contacts().empty(); ++i)
            f.tick();
        check(!f.physics->contacts().empty(), "Contact invalidation precondition missing");
        const auto event = *f.physics->contact_event(0);
        auto entity = f.scene.entity("actor");
        const auto original_settings = entity.get<PhysicsBody>();
        auto invalid = original_settings;
        invalid.mass = -1;
        entity.set<PhysicsBody>(invalid);
        reject([&] { f.physics->synchronize(0); });
        check(f.physics->contact_event(0) && same_contact(*f.physics->contact_event(0), event),
              "Rejected synchronization partially expired the adopted contact list");
        entity.set<PhysicsBody>(original_settings);
        const auto original = f.scene.snapshot();
        auto empty = original;
        empty["entities"] = Json::array();
        f.scene.restore_snapshot(empty);
        check(!f.physics->contact_event(0), "Retired identity exposed an old contact slot");
        f.scene.restore_snapshot(original);
        check(!f.physics->contact_event(0),
              "Same-UUID recreation exposed an old native generation");
        f.physics->synchronize(0);
        check(f.physics->contacts().empty() && !f.physics->contact_event(0),
              "Admitted realization replayed old contacts after same-UUID body recreation");
    }
    {
        Fixture original;
        original.load(moving_sensor_scene());
        const auto actor = *original.engine.world().reference(original.scene.entity("actor").id());
        original.physics->set_linear_velocity(actor, {2, 0, 0});
        for (unsigned i = 0; i < 90 && original.physics->contacts().empty(); ++i)
            original.tick();
        check(!original.physics->contacts().empty(), "Contact recovery precondition missing");
        const auto snapshot = original.scene.snapshot();
        const auto checkpoint = original.physics->checkpoint();
        Fixture recovered;
        recovered.scene.restore_snapshot(snapshot);
        recovered.physics->restore(checkpoint);
        check(recovered.physics->contacts().empty() && !recovered.physics->contact_event(0),
              "Paused recovery replayed transient adopted contacts");
        unsigned begins = 0, ends = 0;
        for (unsigned i = 0; i < 140; ++i) {
            original.tick();
            recovered.tick();
            check(original.physics->body_state(actor) == recovered.physics->body_state(actor) &&
                      original.physics->contacts().size() == recovered.physics->contacts().size(),
                  "Contact recovery changed continued native motion/cache sequence");
            for (std::size_t index = 0; index < original.physics->contacts().size(); ++index) {
                const auto left = original.physics->contact_event(index);
                const auto right = recovered.physics->contact_event(index);
                check(left && right && same_contact(*left, *right),
                      "Recovered contact pair/tick differs from continued native cache");
                begins += right->begin;
                ends += !right->begin;
            }
        }
        check(begins == 0 && ends == 1, "Recovery replayed Begin or lost subsequent sensor End");
    }
}
