#include "model_instance_pose.hpp"
#include "model_render_resource.hpp"
#include "runtime_package.hpp"
#include <forge/authoring.hpp>
#include <forge/editable_mesh.hpp>
#include <forge/runtime.hpp>
#include <fstream>
#include <iostream>
using namespace forge;
using namespace forge::asset_detail;
using namespace std::chrono_literals;
namespace {
void require(bool ok, const std::string& why) {
    if (!ok)
        throw std::runtime_error(why);
}
Json read(const std::filesystem::path& path) {
    Json j;
    std::ifstream(path) >> j;
    return j;
}
void parity(const MeshData& actual, const MeshData& expected) {
    const auto& a = actual.lods.front().parts.front();
    const auto& e = expected.lods.front().parts.front();
    require(a.indices == e.indices && a.streams.size() == e.streams.size(),
            "Shading Gallery cooked topology differs");
    for (const auto& field : e.streams)
        require(a.find(field.semantic) && a.find(field.semantic)->values == field.values,
                "Shading Gallery cooked stream differs");
}
} // namespace
int main(int argc, char** argv) {
    try {
        require(argc == 3, "Need Gallery and scratch root");
        const auto root = std::filesystem::absolute(argv[2]) / AssetId::generate().str();
        std::filesystem::create_directories(root);
        std::filesystem::copy(argv[1], root, std::filesystem::copy_options::recursive);
        const auto authored = read(root / "Scenes/17-mesh-shading.scene.json");
        EngineContext engine(WorldRole::Runtime);
        Scene scene(engine.world());
        scene.restore_snapshot(authored);
        auto catalog = std::make_shared<const AssetCatalog>(AssetCatalog::open_project(root));
        const std::array<const char*, 2> files{"FlatSphere.mesh.json", "SmoothSphere.mesh.json"};
        std::array<EditableMeshSource, 2> sources;
        std::array<AssetId, 2> meshes{};
        for (unsigned i = 0; i < 2; ++i) {
            sources[i] = {read(root / "Assets" / files[i])};
            sources[i].validate();
            meshes[i] = sources[i].asset();
            const auto& selected = catalog->records().at(meshes[i]);
            const auto& meta = selected.metadata.at("forge.import");
            require(selected.source == std::filesystem::path("Assets") / files[i] &&
                        meta.at("platform") == "windows" && meta.at("backend") == "d3d12",
                    "Shading Gallery source/target differs");
            require(sources[i].document.value("smooth", false) == bool(i),
                    "Gallery shading policy differs");
            ResourcePool<MeshAsset> pool;
            auto ticket = request_model_mesh(pool, root, catalog, {meshes[i]});
            require(pool.wait(ticket, 5s) && bool(pool.acquire(ticket)),
                    "Shading Gallery resource load stalled");
            parity(pool.acquire(ticket)->mesh, sources[i].cook());
        }
        auto flat_source = sources[0].document, smooth_source = sources[1].document;
        flat_source.erase("asset_id");
        smooth_source.erase("asset_id");
        flat_source.erase("smooth");
        smooth_source.erase("smooth");
        require(flat_source == smooth_source,
                "Shading spheres differ in authored geometry/UV/color/IDs");
        const auto flat = sources[0].cook(), smooth = sources[1].cook();
        const auto& f = flat.lods[0].parts[0];
        const auto& sm = smooth.lods[0].parts[0];
        require(f.indices == sm.indices && f.find("NORMAL")->values != sm.find("NORMAL")->values,
                "Shading comparison lacks equal topology/distinct normals");
        for (const auto& stream : f.streams)
            if (stream.semantic != "NORMAL")
                require(stream.values == sm.find(stream.semantic)->values,
                        "Shading comparison changed nonnormal stream");
        RuntimeSimulation simulation(engine.world(), scene);
        simulation.reset_presentation();
        const auto render = extract_render_scene(simulation.presentation(1));
        require(!render.cameras.empty() && render.meshes.size() == 3,
                std::string("Shading Gallery camera/renderers missing: cameras=") +
                    std::to_string(render.cameras.size()) +
                    " meshes=" + std::to_string(render.meshes.size()) +
                    " diagnostics=" + std::to_string(render.diagnostics.size()));
        std::set<AssetId> referenced;
        for (const auto& mesh : render.meshes) {
            if (std::find(meshes.begin(), meshes.end(), mesh.renderer.mesh.id) == meshes.end())
                continue; // The shared primitive floor is also extracted as a Mesh.
            referenced.insert(mesh.renderer.mesh.id);
            ResourcePool<MeshAsset> pool;
            const auto ticket = request_model_mesh(pool, root, catalog, mesh.renderer.mesh);
            require(pool.wait(ticket, 5s) && bool(pool.acquire(ticket)),
                    "Shading render consumer unavailable");
            const auto loaded = pool.acquire(ticket);
            const auto& resource = loaded.get();
            const auto selected_materials = select_mesh_materials(resource, mesh.renderer.materials,
                                                                  mesh.renderer.material_variant);
            require(selected_materials.unresolved.empty(),
                    "Gallery authored material slot unresolved in real render consumer");
            require(selected_materials.bindings.size() == 1 &&
                        selected_materials.bindings.front().key == "default",
                    "Authored root Mesh slot key is not default");
            const auto geometry = prepare_mesh_pose_geometry(resource.mesh);
            const auto pose =
                prepare_mesh_instance_pose(resource, geometry, mesh, ModelSceneIndex(render));
            const auto& camera = render.cameras.front();
            const auto view = camera_view(camera.camera, camera.world, 1280, 720);
            require(bounds_visible(mesh_instance_bounds(pose, view.position), view),
                    "Shading Gallery panel outside game camera");
        }
        require(referenced == std::set<AssetId>(meshes.begin(), meshes.end()),
                "Shading Gallery scene references differ");
        const std::array roots{authored.at("asset_id").get<AssetId>()};
        auto lease = std::make_shared<ProjectLease>(root);
        prepare_runtime_content_catalog(*lease, roots, {});
        const auto exported = root.parent_path() / (root.filename().string() + "-export");
        (void)package_runtime_content(root, exported, roots, {"windows", "d3d12"});
        const auto moved = std::filesystem::path(exported.string() + "-relocated");
        std::filesystem::rename(exported, moved);
        auto shipped =
            std::make_shared<const AssetCatalog>(open_runtime_content(moved, {"windows", "d3d12"}));
        std::filesystem::rename(root / "Assets", root / "source-offline");
        for (unsigned i = 0; i < 2; ++i) {
            require(!std::filesystem::exists(moved / "Assets" / files[i]),
                    "Shading source leaked into game export");
            ResourcePool<MeshAsset> pool;
            const auto ticket = request_model_mesh(pool, moved, shipped, {meshes[i]});
            require(pool.wait(ticket, 5s) && bool(pool.acquire(ticket)),
                    "Relocated shading resource unavailable");
            parity(pool.acquire(ticket)->mesh, sources[i].cook());
        }
        require(shipped->records().size() == 3,
                "Shading scene omitted selected Mesh dependency closure");
        std::cout << "Shading Gallery topology/seams/source/cook/resource/camera and relocated "
                     "source-free export passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
