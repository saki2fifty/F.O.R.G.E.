#include "fixtures/mesh_foliage_gallery.hpp"
#include <iostream>

using namespace forge;
int main(int argc, char** argv) {
    try {
        using namespace test_mesh_foliage_gallery;
        check(argc == 3, "Need original Gallery and isolated Mesh foliage project output");
        const auto root = std::filesystem::absolute(argv[2]) / AssetId::generate().str();
        std::filesystem::create_directories(root.parent_path());
        std::filesystem::copy(argv[1], root, std::filesystem::copy_options::recursive);
        if (!std::filesystem::exists(root / scene_path))
            author(root);
        auto lease = std::make_shared<ProjectLease>(root);
        imported_profile(*lease);
        EngineContext engine;
        Scene scene(engine.world());
        scene.reset(read(root / scene_path));
        std::string surface, prototype;
        const auto before = scene.document();
        for (const auto& entity : before.at("entities")) {
            const auto name = entity.at("name").get<std::string>();
            if (name.starts_with("Mesh surface - tilted"))
                surface = entity.at("id");
            if (name.starts_with("Blade template"))
                prototype = entity.at("id");
        }
        check(!surface.empty() && !prototype.empty(), "Saved Mesh meadow actors missing");
        detail::MeshScatterSettings settings;
        settings.center = {-7, 0, 0};
        settings.count = 12;
        settings.radius = 4.6;
        settings.spacing = .55;
        settings.seed = 29;
        const auto arguments =
            detail::mesh_scatter_arguments(prototype, settings, "Reopened Mesh patch");
        const auto plan = prepare_mesh_foliage(*lease, scene, arguments, surface, 0, {}, true);
        check(scene.document() == before && plan.at("retained_count") == 12,
              "Reopened published Mesh preparation changed Scene or lost candidates");
        const auto placed = authoring_command(scene, "mesh.scatter", plan.at("arguments"));
        const auto after = scene.document();
        check(placed.at("created").size() == 12 && scene.undo() && scene.document() == before &&
                  scene.redo() && scene.document() == after,
              "Reopened Mesh placement did not use one Scene history entry");
        scene.save(root / "Scenes/reopened-mesh-patch.scene.json");
        check(read(root / "Scenes/reopened-mesh-patch.scene.json") == after,
              "Reopened Mesh patch changed during save");
        Json dabs = Json::array();
        for (const double x : {-8., -6.}) {
            settings.center = {x, 0, 0};
            settings.radius = .8;
            settings.spacing = .1;
            auto dab = detail::mesh_scatter_arguments(prototype, settings, "Stroke dab");
            dab.erase("entity");
            dab.erase("name");
            dabs.push_back(std::move(dab));
        }
        const Json stroke{{"entity", prototype}, {"name", "Projected stroke"}, {"dabs", dabs}};
        const auto prepared =
            prepare_foliage_stroke(*lease, scene, stroke, surface, true, .1, {}, true);
        check(scene.document() == after && prepared.at("candidate_count") == 24 &&
                  prepared.at("retained_count") == 24 &&
                  prepared.at("surface_revision") == plan.at("surface_revision"),
              "Stroke preparation changed intent or lost the shared published selection");
        const auto brush = authoring_command(scene, "mesh.stroke", prepared.at("arguments"));
        check(brush.at("created").size() == 24 && scene.undo() && scene.document() == after,
              "Projected stroke did not publish one complete ordinary Scene transaction");
        for (unsigned invalid = 0; invalid < 4; ++invalid) {
            auto bad = stroke;
            if (invalid == 0)
                bad["dabs"][1]["center"]["x"] = 10000;
            if (invalid == 1)
                bad["dabs"][1]["world_heights"] = Json::array({1});
            if (invalid == 2)
                for (auto& dab : bad["dabs"])
                    dab["count"] = 257;
            if (invalid == 3)
                bad["name"] = " ";
            bool rejected = false;
            try {
                (void)prepare_foliage_stroke(*lease, scene, bad, surface, true);
            } catch (const AuthoringError&) {
                rejected = true;
            }
            check(rejected && scene.document() == after,
                  "Invalid late stroke preparation published partial Scene intent");
        }
        std::cout << "Mesh foliage project: real static/skinned Model profile, published root, "
                     "reopened Scene, preparation, placement, history and save pass\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
