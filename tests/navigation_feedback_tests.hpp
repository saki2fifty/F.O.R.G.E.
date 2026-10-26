#pragma once
#include "json_output_text.hpp"
inline void navigation_feedback_tests(const std::filesystem::path& root, const Json& source,
                                      bool check_limit = false) {
    Fixture run(root);
    run.scene.restore_snapshot(source);
    auto actor = run.scene.entity("agent");
    const auto reference = run.engine.world().reference(actor.id()).value();
    const auto debug = [&] { return run.navigation->debug(actor.id()); };
    actor.set<PhysicsBody>({});
    run.tick();
    check(debug().at("phase") == "Failed" &&
              debug().at("diagnostic").get<std::string>().find("physics body") != std::string::npos,
          "Navigation snapshot did not explain the actual pose-ownership failure");
    const auto checkpoint = run.navigation->checkpoint();
    for (unsigned read = 0; read < 20; ++read)
        (void)debug();
    check(run.navigation->checkpoint() == checkpoint && checkpoint.at("assets").empty(),
          "Reading a failed agent loaded navigation or mutated recovery state");
    run.navigation->tune(reference, "forge.navigation_agent", "speed", 3);
    check(debug().at("phase") == "Pending" && debug().at("diagnostic") == "",
          "A changed configuration retained the previous failure reason");
    actor.remove<PhysicsBody>();
    run.tick();
    check(debug().at("sampled") && debug().at("diagnostic") == "",
          "A corrected agent retained a stale ownership failure");
    run.navigation->tune(reference, "forge.navigation_agent", "enabled", false);
    check(debug().at("phase") == "Disabled" && debug().at("diagnostic") == "",
          "Disabled navigation retained a prior failure reason");
    run.navigation->tune(reference, "forge.navigation_agent", "enabled", true);
    run.navigation->tune(reference, "forge.navigation_agent", "has_destination", false);
    check(debug().at("phase") == "Idle" && debug().at("diagnostic") == "",
          "Cleared destination retained a prior failure reason");
    run.navigation->tune(reference, "forge.navigation_agent", "destination_x", 100);
    run.navigation->tune(reference, "forge.navigation_agent", "has_destination", true);
    run.tick();
    check(debug().at("phase") == "Failed" && !debug().at("has_path").get<bool>() &&
              !debug().at("diagnostic").get<std::string>().empty(),
          "Unreachable route lacked a concrete navigation diagnostic");
    run.navigation->tune(reference, "forge.navigation_agent", "destination_x", 8);
    check(debug().at("diagnostic") == "", "Pending retarget exposed a stale route failure");
    run.tick();
    const auto floor = run.scene.entity("floor");
    const auto translation = floor.get<LocalTranslation>();
    floor.set<LocalTranslation>({translation.x + 1, translation.y, translation.z});
    run.tick();
    check(debug().at("phase") == "Failed" &&
              debug().at("diagnostic").get<std::string>().find("rebuild") != std::string::npos,
          "Stale source failure did not explain that a rebuild is required");
    floor.set<LocalTranslation>(translation);
    run.tick();
    check(debug().at("sampled") && debug().at("diagnostic") == "",
          "Restored matching sources retained stale failure text");

    const auto parent_id = authoring_command(run.scene, "entity.create",
                                             {{"kind", 4}, {"name", "Moving \xe7\x95\x8c parent"}})
                               .at("selected")
                               .get<std::string>();
    const auto parent = run.scene.entity(parent_id);
    parent.set<CharacterController2D>({});
    const auto binding = actor.get<SpatialBinding>();
    actor.set<SpatialBinding>({SpatialMode::Explicit, run.scene.reference(parent_id)});
    run.tick();
    const auto parent_failure = debug().at("diagnostic").get<std::string>();
    check(debug().at("phase") == "Failed" && parent_failure.find(parent_id) != std::string::npos &&
              parent_failure.find("Moving \xe7\x95\x8c parent") != std::string::npos,
          "Ancestor failure did not identify its authored name and persistent identity");
    actor.set<SpatialBinding>(binding);
    run.tick();
    check(debug().at("diagnostic") == "", "Corrected ancestry retained an old failure message");

    if (check_limit) {
        Fixture crowded(root);
        crowded.scene.restore_snapshot(source);
        std::vector<std::string> extras;
        const auto add = [&] {
            const auto id =
                authoring_command(crowded.scene, "entity.create", {{"name", "Limit participant"}})
                    .at("selected")
                    .get<std::string>();
            auto config = NavigationAgent{};
            config.enabled = false;
            crowded.scene.entity(id).set<NavigationAgent>(config);
            return id;
        };
        for (unsigned i = 0; i < 64; ++i)
            extras.push_back(add());
        const auto warnings = [&] {
            const auto diagnostics = crowded.engine.services().diagnostics();
            return std::count_if(diagnostics.begin(), diagnostics.end(), [](const auto& record) {
                return record.at("text").template get<std::string>().find(
                           "exceeds 64 authored agents") != std::string::npos;
            });
        };
        crowded.tick();
        check(warnings() == 1 && crowded.navigation->debug(crowded.scene.entity("agent").id())
                                         .at("diagnostic")
                                         .get<std::string>()
                                         .find("64") != std::string::npos,
              "Global agent limit was not attributed in the selected agent's feedback");
        crowded.scene.entity(extras.back()).destruct();
        crowded.tick();
        check(crowded.navigation->debug(crowded.scene.entity("agent").id()).at("diagnostic") == "",
              "Resolved global limit remained visible on a valid agent");
        (void)add();
        crowded.tick();
        check(warnings() == 2, "A later agent-limit episode did not emit a fresh diagnostic");
    }
    const std::string unicode = "Native \xe7\x95\x8c \xf0\x9f\x98\x80";
    check(detail::json_output_text(unicode) == unicode, "Display conversion changed valid Unicode");
    const std::string malformed = "bad \xff \xe2\x82";
    const auto clean = detail::json_output_text(malformed);
    check(Json::parse(clean.dump()) == clean &&
              clean.get<std::string>().find("\xef\xbf\xbd") != std::string::npos,
          "Native display text cannot survive strict JSON serialization");
    reject([&] { (void)Json(malformed).dump(); });
    const auto raw = std::string(2047, 'a') + "\xe7\x95\x8c";
    const auto clipped = detail::json_output_text(raw.substr(0, 2048));
    check(clipped.get<std::string>().ends_with("\xef\xbf\xbd") && raw.size() == 2050,
          "Bounded native output split a character without a readable replacement");
    std::cout
        << "Navigation concrete feedback, stale suppression, pure reads and output text passed\n";
}
