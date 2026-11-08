#include "mesh_scatter_authoring.hpp"
#include <forge/authoring.hpp>
#include <iostream>

using namespace forge;
namespace {
void check(bool value, const char* why) {
    if (!value)
        throw std::runtime_error(why);
}
template <class F> void rejects(F operation) {
    try {
        operation();
    } catch (const AuthoringError&) {
        return;
    }
    throw std::runtime_error("Invalid foliage stroke was admitted");
}
} // namespace
int main() {
    try {
        EngineContext engine;
        Scene scene(engine.world());
        const auto source =
            authoring_command(scene, "entity.create",
                              {{"recipe", "primitive.0"}, {"name", "Brush template"}})
                .at("selected")
                .get<std::string>();
        detail::MeshScatterSettings settings;
        settings.count = 4;
        settings.spacing = 0;
        settings.radius = 1;
        settings.seed = 7;
        Json dabs = Json::array();
        for (const auto center : {Double3{-2, 1, 4}, Double3{3, -2, -1}}) {
            settings.center = center;
            auto dab = detail::mesh_scatter_arguments(source, settings, "Ignored dab name");
            dab.erase("entity");
            dab.erase("name");
            dabs.push_back(dab);
        }
        Json arguments{{"entity", source}, {"name", "One brush stroke"}, {"dabs", dabs}};
        const auto before = scene.document();
        const auto preview = preview_authoring(
            scene, Json::array({{{"operation", "mesh.stroke"}, {"arguments", arguments}}}));
        check(scene.document() == before && preview.at("entities").size() == 12,
              "Detached stroke preview published or lost ordinary objects");
        const auto placed = authoring_command(scene, "mesh.stroke", arguments);
        const auto after = scene.document();
        check(placed.at("groups").size() == 2 && placed.at("created").size() == 8 &&
                  placed.at("candidate_count") == 8 && placed.at("placed_count") == 8,
              "Stroke did not compose ordinary scatter results");
        const auto effective = scene.effective_document();
        for (unsigned dab = 0; dab < 2; ++dab) {
            settings.center = dab ? Double3{3, -2, -1} : Double3{-2, 1, 4};
            const auto layout = detail::mesh_scatter_layout(settings);
            unsigned count = 0;
            for (const auto& row : effective.at("entities")) {
                if (row.at("id") == placed.at("groups")[dab])
                    check(row.at("parent") == placed.at("selected"), "Dab escaped stroke root");
                if (row.value("parent", Json()) != placed.at("groups")[dab])
                    continue;
                const auto& m = row.at("world_affine");
                check(std::abs(m[3].get<double>() - settings.center[0] - layout[count].offset[0]) <
                              1e-10 &&
                          std::abs(m[7].get<double>() - settings.center[1]) < 1e-10 &&
                          std::abs(m[11].get<double>() - settings.center[2] -
                                   layout[count].offset[1]) < 1e-10,
                      "Preserve-world grouping changed seeded dab origins");
                ++count;
            }
            check(count == 4, "Stroke lost dab children");
        }
        check(scene.undo() && scene.document() == before && scene.redo() &&
                  scene.document() == after,
              "Stroke did not use one existing Scene history entry");
        auto invalid = arguments;
        invalid["dabs"][1]["world_heights"] = Json::array({1});
        rejects([&] { authoring_command(scene, "mesh.stroke", invalid); });
        check(scene.document() == after, "Late invalid dab partially published stroke");
        invalid = arguments;
        invalid["dabs"][0]["count"] = 257;
        invalid["dabs"][1]["count"] = 257;
        rejects([&] { authoring_command(scene, "mesh.stroke", invalid); });
        invalid = arguments;
        invalid["name"] = "   ";
        rejects([&] { authoring_command(scene, "mesh.stroke", invalid); });
        invalid = arguments;
        invalid["dabs"] = Json::array();
        rejects([&] { authoring_command(scene, "mesh.stroke", invalid); });
        check(scene.document() == after, "Rejected stroke changed saved intent");
        std::cout << "Atomic foliage stroke composition, world placement, limits/history and "
                     "rollback pass\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
