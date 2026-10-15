#include "material_parameter_overrides.hpp"
#include "model_draw_candidate.hpp"
#include "runtime_package.hpp"
#include <forge/game_content.hpp>
#include <forge/render_scene.hpp>
#include <forge/scene.hpp>
#include <fstream>
#include <iostream>
using namespace forge;
using namespace forge::asset_detail;
namespace {
void require(bool value, const char* why) {
    if (!value)
        throw std::runtime_error(why);
}
Json read(const std::filesystem::path& path) {
    std::ifstream file(path);
    Json value;
    file >> value;
    return value;
}
void verify(const std::filesystem::path& project, AssetId scene_id, AssetId material_id) {
    WorldContext world(WorldRole::Runtime);
    Scene scene(world);
    const auto source = load_game_scene(project, {scene_id});
    scene.restore_snapshot(source);
    const auto effective = scene.effective_document();
    const auto draws = extract_render_scene(effective);
    require(draws.meshes.size() == 7 && draws.cameras.size() == 1 && draws.lights.size() == 1 &&
                draws.diagnostics.empty(),
            "Gallery66 did not extract its six cubes, floor, camera and light");
    const auto catalog = std::make_shared<const AssetCatalog>(AssetCatalog::open_project(project));
    ResourcePool<MeshAsset> meshes;
    ResourcePool<MaterialAsset> materials;
    const auto ticket = request_model_pbr_material(materials, project, catalog, {material_id});
    require(materials.wait(ticket, std::chrono::seconds(10)),
            "Gallery66 shared material failed to load");
    const auto material = materials.acquire(ticket);
    const auto shared_values = material->values;
    std::map<std::string, std::vector<MaterialParameterOverride>> admitted;
    std::set<ResourceIdentity> identities;
    std::optional<PreparedModelDrawKey> shared_key;
    for (const auto& row : effective.at("entities")) {
        const auto entity = scene.entity(row.at("id").get<std::string>());
        if (!entity.has<MeshRenderer>())
            continue; // Legacy floor still uses the same extracted renderer path.
        const auto& renderer = entity.get<MeshRenderer>();
        require(renderer.materials.size() == 1 && renderer.materials[0].material.id == material_id,
                "Gallery66 duplicated or retargeted its shared material");
        const auto mesh_ticket = request_model_mesh(meshes, project, catalog, renderer.mesh);
        require(meshes.wait(mesh_ticket, std::chrono::seconds(10)),
                "Gallery66 cube failed to load");
        PreparedModelDraw prepared;
        prepared.mesh = meshes.acquire(mesh_ticket);
        prepared.selection = select_mesh_materials(prepared.mesh.get(), renderer.materials);
        prepared.materials.emplace(material_id, material);
        identities.insert(material.identity());
        const auto key = prepared_model_draw_key(prepared, false);
        if (!shared_key)
            shared_key = key;
        require(key == *shared_key, "Object numeric values split physical resource identity");
        admitted[row.at("name").get<std::string>()] =
            prepare_model_material_parameters(prepared, renderer.material_parameters);
    }
    require(admitted.size() == 6 && identities.size() == 1 && material->values == shared_values &&
                admitted.at("Material default").empty() &&
                detail::same_material_parameter_values(admitted.at("Green - shared values A"),
                                                       admitted.at("Green - shared values B")) &&
                !detail::same_material_parameter_values(admitted.at("Red - polished"),
                                                        admitted.at("Blue - matte")),
            "Gallery66 lost independent/shared/default object value behavior");
    const auto instance =
        std::find_if(source.at("entities").begin(), source.at("entities").end(),
                     [](const auto& row) { return row.contains("prefab_instance"); });
    require(instance != source.at("entities").end() &&
                instance->at("name") == "Prefab instance - amber" &&
                !instance->at("components").contains("forge.mesh_renderer") &&
                instance->at("property_overrides")
                    .at("forge.mesh_renderer")
                    .contains("material_parameters"),
            "Gallery66 flattened per-instance prefab property intent");
}
} // namespace
int main(int argc, char** argv) {
    try {
        require(argc == 3, "Need Gallery project and scratch root");
        const auto root = std::filesystem::absolute(argv[2]) / AssetId::generate().str();
        const auto source = root / "source";
        const auto output = root / "export";
        std::filesystem::create_directories(root);
        std::filesystem::copy(argv[1], source, std::filesystem::copy_options::recursive);
        const auto scene = read(source / "Scenes/66-object-material-values.scene.json");
        const auto scene_id = scene.at("asset_id").get<AssetId>();
        const auto material_id = read(source / "Assets/ObjectValues/SharedSurface.material.json")
                                     .at("asset_id")
                                     .get<AssetId>();
        verify(source, scene_id, material_id);
        const std::array roots{scene_id};
        const RuntimePackageTarget target{"windows", "d3d12"};
        (void)package_runtime_content(source, output, roots, target);
        std::filesystem::rename(source, root / "offline-source");
        const auto relocated = root / "relocated-game";
        std::filesystem::rename(output, relocated);
        const auto catalog = open_runtime_content(relocated, target);
        require(std::count_if(catalog.records().begin(), catalog.records().end(),
                              [](const auto& item) {
                                  return item.second.type == MaterialAsset::type;
                              }) == 1 &&
                    catalog.records().contains(material_id),
                "Gallery66 export duplicated the shared material asset");
        verify(relocated, scene_id, material_id);
        require(!std::filesystem::exists(relocated /
                                         "Assets/ObjectValues/SharedSurface.material.json") &&
                    !std::filesystem::exists(relocated / "CodeExamples"),
                "Gallery66 export depended on material sources or C++ source examples");
        std::cout << relocated.string() << '\n';
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
