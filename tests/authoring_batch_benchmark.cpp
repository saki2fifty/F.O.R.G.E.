#include <algorithm>
#include <charconv>
#include <chrono>
#include <forge/authoring.hpp>
#include <forge/entity_recipes.hpp>
#include <forge/world.hpp>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {
using namespace forge;
using Clock = std::chrono::steady_clock;
void check(bool value, const char* reason) {
    if (!value)
        throw std::runtime_error(reason);
}
unsigned integer(const char* text) {
    unsigned value = 0;
    const std::string_view input(text);
    const auto result = std::from_chars(input.data(), input.data() + input.size(), value);
    check(result.ec == std::errc{} && result.ptr == input.data() + input.size(),
          "Benchmark arguments must be unsigned integers");
    return value;
}
template <class Action> double milliseconds(Action&& action) {
    const auto start = Clock::now();
    action();
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}
} // namespace
int main(int argc, char** argv) {
    try {
        check(argc == 6, "Need create|rename|move, existing entities, commands, warmups, samples");
        const std::string operation = argv[1];
        const unsigned entities = integer(argv[2]), count = integer(argv[3]),
                       warmups = integer(argv[4]), samples = integer(argv[5]);
        check(operation == "create" || operation == "rename" || operation == "move",
              "Unknown benchmark workload");
        check(count && count <= 128 && entities >= count && entities <= 10000 - count &&
                  warmups <= 8 && samples && samples <= 16,
              "Benchmark exceeds the supported workload bounds");
        EngineContext engine;
        Scene scene(engine.world());
        auto source = scene.document();
        auto ids = std::vector<std::string>{};
        ids.reserve(entities);
        for (unsigned i = 0; i < entities; ++i) {
            const auto id = EntityId::generate().str();
            ids.push_back(id);
            Json row{{"id", id},
                     {"name", "Existing " + std::to_string(i)},
                     {"spatial", {{"mode", "world"}}},
                     {"components",
                      {{"forge.local_translation", {{"x", double(i)}, {"y", 0.}, {"z", 0.}}},
                       {"forge.local_rotation", {{"x", 0.}, {"y", 0.}, {"z", 0.}, {"w", 1.}}},
                       {"forge.local_scale", {{"x", 1.}, {"y", 1.}, {"z", 1.}}}}}};
            if (i % 7 == 0)
                row["components"]["benchmark.opaque"] = {{"label", "Retain unknown component data"},
                                                         {"values", {1, 2, 3}}};
            source["entities"].push_back(std::move(row));
        }
        scene.edit(source);
        const auto baseline = scene.document();
        const auto identity = scene.asset_id();
        const auto bytes = baseline.dump().size();
        check(bytes < 8 * 1024 * 1024, "Benchmark source exceeds the authoring byte limit");
        std::map<std::string, flecs::entity_t> handles;
        for (unsigned i = 0; i < std::min(entities, 8u); ++i)
            handles.emplace(ids[i], scene.entity(ids[i]).id());
        std::string recipe;
        for (const auto& candidate : entity_recipes())
            if (candidate.kind == 0) {
                recipe = candidate.id;
                break;
            }
        check(!recipe.empty(), "No current Cube composition recipe");
        Json measured = Json::array();
        for (unsigned sample = 0; sample < warmups + samples; ++sample) {
            auto commands = Json::array();
            for (unsigned i = 0; i < count; ++i) {
                const auto name = "Batch " + std::to_string(sample) + " - " + std::to_string(i);
                if (operation == "create")
                    commands.push_back(
                        {{"operation", "entity.create"},
                         {"arguments",
                          {{"recipe", recipe},
                           {"name", name},
                           {"position", {{"x", double(i)}, {"y", 2.}, {"z", 3.}}}}}});
                else if (operation == "rename")
                    commands.push_back({{"operation", "entity.rename"},
                                        {"arguments", {{"entity", ids[i]}, {"name", name}}}});
                else
                    commands.push_back(
                        {{"operation", "transform.position"},
                         {"arguments",
                          {{"entity", ids[i]},
                           {"value", {{"x", double(i) + 1.}, {"y", 2.}, {"z", 3.}}}}}});
            }
            const auto before = scene.document();
            const auto revision = scene.revision();
            Json preview;
            const auto preview_ms =
                milliseconds([&] { preview = preview_authoring(scene, commands); });
            check(scene.revision() == revision && scene.document() == before,
                  "Preview changed authored content or revision");
            Json result;
            const auto apply_ms =
                milliseconds([&] { result = apply_authoring(scene, commands, revision); });
            const auto after = scene.document();
            check(result.at("changed") && scene.revision() == revision + 1 &&
                      result.at("results").size() == count && scene.asset_id() == identity,
                  "Batch did not publish one expected revision with stable identity");
            check(scene.entity_count() == entities + (operation == "create" ? count : 0),
                  "Batch produced an unexpected entity count");
            if (operation != "create")
                check(preview == after, "Prepared values differ from the committed values");
            std::map<std::string, const Json*> rows;
            for (const auto& row : after.at("entities"))
                rows.emplace(row.at("id").get<std::string>(), &row);
            for (unsigned i = 0; i < count; ++i) {
                const auto id = operation == "create"
                                    ? result.at("results").at(i).at("selected").get<std::string>()
                                    : ids[i];
                const auto& row = *rows.at(id);
                if (operation == "move") {
                    const auto& value = row.at("components").at("forge.local_translation");
                    check(value.at("x") == double(i) + 1. && value.at("y") == 2. &&
                              value.at("z") == 3.,
                          "Moved value differs from its independent expected position");
                } else
                    check(row.at("name") ==
                              "Batch " + std::to_string(sample) + " - " + std::to_string(i),
                          "Created/renamed entity differs from its expected value");
            }
            double undo_ms = milliseconds([&] { check(scene.undo(), "Batch Undo missing"); });
            check(scene.document() == before, "One Undo did not restore the exact input");
            double redo_ms = milliseconds([&] { check(scene.redo(), "Batch Redo missing"); });
            check(scene.document() == after, "Redo did not restore the exact batch");
            check(scene.undo() && scene.document() == baseline, "Benchmark reset failed");
            for (const auto& [id, handle] : handles)
                check(scene.entity(id).id() == handle, "Surviving native entity handle changed");
            if (sample >= warmups)
                measured.push_back({{"preview_ms", preview_ms},
                                    {"apply_ms", apply_ms},
                                    {"undo_ms", undo_ms},
                                    {"redo_ms", redo_ms}});
        }
        const auto before_failure = scene.document();
        const auto revision = scene.revision();
        const bool undo_available = scene.can_undo(), redo_available = scene.can_redo();
        bool rejected = false;
        try {
            (void)apply_authoring(
                scene,
                Json::array({{{"operation", "entity.create"}, {"arguments", {{"recipe", recipe}}}},
                             {{"operation", "entity.rename"},
                              {"arguments", {{"entity", ids[0]}, {"name", "   "}}}}}),
                revision);
        } catch (const std::exception&) {
            rejected = true;
        }
        check(rejected && scene.document() == before_failure && scene.revision() == revision &&
                  scene.can_undo() == undo_available && scene.can_redo() == redo_available,
              "Failed tail command changed authored content, revision or history");
        std::cout << Json{{"workload", operation},
                          {"existing_entities", entities},
                          {"commands", count},
                          {"source_bytes", bytes},
                          {"recipe", recipe},
                          {"warmups", warmups},
                          {"samples", measured},
                          {"verified_values", true},
                          {"verified_history", true},
                          {"verified_rollback", true},
                          {"verified_survivor_handles", true},
                          {"scope",
                           "Owner-thread CPU authoring only; no editor/GPU/game performance claim"}}
                         .dump()
                  << '\n';
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
