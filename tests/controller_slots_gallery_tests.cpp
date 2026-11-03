#include "runtime_package.hpp"
#include <algorithm>
#include <array>
#include <filesystem>
#include <forge/authoring.hpp>
#include <forge/project.hpp>
#include <forge/runtime.hpp>
#include <forge/scene_identity.hpp>
#include <fstream>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

using namespace forge;
namespace {
constexpr auto scene_path = "Scenes/149-sixteen-controllers.scene.json";
constexpr auto header_path = "CodeExamples/ControllerSlots.hpp";
void check(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
Json read(const std::filesystem::path& path) {
    std::ifstream file(path);
    return Json::parse(file);
}
void author(const std::filesystem::path& root) {
    check(!std::filesystem::exists(root / scene_path) &&
              !std::filesystem::exists(root / header_path),
          "Controller Gallery already exists");
    ProjectLease lease(root);
    EngineContext engine;
    Scene scene(engine.world());
    auto definition = duplicate_scene_asset(read(root / "Scenes/92-multiple-gamepads.scene.json"),
                                            AssetId::generate(), scene.schema());
    const auto prototypes = definition.at("entities");
    check(prototypes.size() == 5, "Original controller Gallery layout changed");
    definition["entities"] = Json::array({prototypes.front()});
    definition["entities"][0]["name"] = "Sixteen-controller canvas camera";
    ProjectSettings settings(root);
    const auto original = settings.document();
    auto candidate = original;
    std::vector<std::string> markers, actions;
    for (std::size_t i = 0; i < gamepad_slot_limit; ++i) {
        const auto slot = i + 1;
        auto marker = prototypes.at(1 + i % 4);
        if (i >= 4)
            marker["id"] = EntityId::generate();
        marker["name"] = "Player " + std::to_string(slot) + " - left stick";
        marker["components"]["forge.local_translation"] = {
            {"x", -4.5 + 3 * (i % 4)}, {"y", 3. - 2 * (i / 4)}, {"z", 0}};
        marker["components"]["forge.sprite_renderer"]["width"] = 1.;
        marker["components"]["forge.sprite_renderer"]["height"] = 1.;
        markers.push_back(marker.at("id").get<std::string>());
        definition["entities"].push_back(std::move(marker));
        const auto control = gamepad_control_id(std::uint32_t(slot), "left_x");
        const auto found = std::find_if(candidate["input"]["actions"].begin(),
                                        candidate["input"]["actions"].end(), [&](const auto& row) {
                                            return row.at("bindings").size() == 2 &&
                                                   row.at("bindings")[0].at("control") == control;
                                        });
        if (found != candidate["input"]["actions"].end()) {
            actions.push_back(found->at("id").template get<std::string>());
        } else {
            const auto id = ActionId::generate();
            actions.push_back(id.str());
            candidate["input"]["actions"].push_back(
                {{"id", id},
                 {"name", "Player " + std::to_string(slot) + " move"},
                 {"kind", "axis2"},
                 {"bindings",
                  Json::array({{{"control", control},
                                {"x", 1},
                                {"y", 0},
                                {"radial", true},
                                {"deadzone", .2}},
                               {{"control", gamepad_control_id(std::uint32_t(slot), "left_y")},
                                {"x", 0},
                                {"y", -1},
                                {"radial", true},
                                {"deadzone", .2}}})}});
        }
    }
    scene.reset(definition);
    ProjectSettings::validate(candidate);
    scene.save(root / scene_path);
    settings.save(candidate, &original);
    std::ofstream header(root / header_path);
    header << "#pragma once\n#include <array>\nnamespace forge::examples {\n"
              "// Authored Gallery identities shared by the optional SDK example.\n"
              "inline constexpr auto controller_slots_scene = "
           << definition.at("asset_id").dump() << ";\n";
    for (const auto& [name, values] : {std::pair{"controller_slot_markers", markers},
                                       std::pair{"controller_slot_actions", actions}}) {
        header << "inline constexpr std::array<const char*, 16> " << name << "{\n";
        for (std::size_t i = 0; i < values.size(); ++i)
            header << "    " << Json(values[i]).dump() << (i + 1 < values.size() ? ",\n" : "};\n");
    }
    header << "} // namespace forge::examples\n";
    header.close();
    check(bool(header), "Cannot save controller example identities");
    auto catalog = AssetCatalog::open_project(root);
    const auto record = catalog.add_scene(scene_path);
    catalog.save(AssetCatalog::project_index(root));
    const std::array roots{record.id};
    prepare_runtime_content_catalog(lease, roots, {}, scene.schema());
}
void packaged(const std::filesystem::path& gallery, const std::filesystem::path& scratch) {
    const auto stage = std::filesystem::absolute(scratch) / AssetId::generate().str();
    const auto root = stage / "source";
    std::filesystem::create_directories(stage);
    std::filesystem::copy(gallery, root, std::filesystem::copy_options::recursive);
    const auto original = read(root / scene_path);
    check(original.at("entities").size() == 17, "Sixteen-controller Gallery lost markers");
    const std::array roots{original.at("asset_id").get<AssetId>()};
    EngineContext engine;
    Scene scene(engine.world());
    scene.reset(original);
    {
        ProjectLease lease(root);
        prepare_runtime_content_catalog(lease, roots, {}, scene.schema());
        package_runtime_content(root, stage / "export", roots, {"windows", "d3d12"}, {}, {},
                                scene.schema());
    }
    std::filesystem::rename(stage / "export", stage / "relocated");
    std::filesystem::rename(root, stage / "sources-hidden");
    const auto catalog = open_runtime_content(stage / "relocated", {"windows", "d3d12"});
    const auto record = catalog.resolve(AssetRef<SceneAsset>{roots.front()});
    check(record.state == AssetState::Available &&
              read(stage / "relocated" / record.record->source) == original,
          "Relocated content lost the sixteen-marker Scene or its identities");
    check(!std::filesystem::exists(stage / "relocated/CodeExamples"),
          "Content-only export retained optional source code");
    check(read(gallery / scene_path) == original, "Gallery source changed during content export");
}
} // namespace
int main(int argc, char** argv) {
    try {
        if (argc == 3 && std::string(argv[1]) == "--author")
            author(std::filesystem::absolute(argv[2]));
        else if (argc == 3)
            packaged(std::filesystem::absolute(argv[1]), std::filesystem::absolute(argv[2]));
        else
            throw std::runtime_error("Need Gallery and scratch, or --author Gallery");
        std::cout << "Controller Gallery authoring/content export passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
