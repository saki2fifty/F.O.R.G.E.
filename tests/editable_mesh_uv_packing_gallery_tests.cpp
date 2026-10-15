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
namespace {
void require(bool ok, const std::string& why) {
    if (!ok)
        throw std::runtime_error(why);
}
Json read(const std::filesystem::path& path) {
    Json value;
    std::ifstream(path) >> value;
    return value;
}
void parity(const MeshData& actual, const MeshData& expected) {
    const auto& a = actual.lods[0].parts[0];
    const auto& e = expected.lods[0].parts[0];
    require(a.indices == e.indices && a.streams.size() == e.streams.size(),
            "Packed Gallery topology differs");
    for (const auto& stream : e.streams)
        require(a.find(stream.semantic) && a.find(stream.semantic)->values == stream.values,
                "Packed Gallery resource stream differs");
}
} // namespace
int main(int argc, char** argv) {
    try {
        require(argc == 3, "Need Gallery and packing scratch root");
        const auto root = std::filesystem::absolute(argv[2]) / AssetId::generate().str();
        std::filesystem::create_directories(root);
        std::filesystem::copy(argv[1], root, std::filesystem::copy_options::recursive);
        const auto authored = read(root / "Scenes/44-uv-island-packing.scene.json");
        const std::array<const char*, 3> files{"UvPackingOriginal.mesh.json",
                                               "UvPackingPacked.mesh.json",
                                               "UvPackingRotated.mesh.json"};
        std::array<EditableMeshSource, 3> sources;
        std::array<AssetId, 3> meshes{};
        auto catalog = std::make_shared<const AssetCatalog>(AssetCatalog::open_project(root));
        for (unsigned i = 0; i < 3; ++i) {
            sources[i] = {read(root / "Assets" / files[i])};
            sources[i].validate();
            meshes[i] = sources[i].asset();
            require(sources[i].document.at("vertices").size() == 24 &&
                        sources[i].document.at("faces").size() == 6 &&
                        sources[i].document.at("next_id") == 31 &&
                        sources[i].uv_islands().size() == 6,
                    "Packed Gallery source profile differs");
            if (i) {
                auto expected = sources[0];
                expected.document["asset_id"] = meshes[i];
                expected.pack_uv_islands(std::array<std::uint32_t, 6>{25, 26, 27, 28, 29, 30}, .025,
                                         i == 2);
                require(expected.document == sources[i].document,
                        "Packed Gallery does not match its original source operation");
            }
            const auto& meta = catalog->records().at(meshes[i]).metadata.at("forge.import");
            require(meta.at("platform") == "windows" && meta.at("backend") == "d3d12",
                    "Packed Gallery target mismatch");
            ResourcePool<MeshAsset> pool;
            const auto ticket = request_model_mesh(pool, root, catalog, {meshes[i]});
            require(pool.wait(ticket, std::chrono::seconds(5)) && bool(pool.acquire(ticket)),
                    "Packed Gallery mesh load failed");
            const auto resource = pool.acquire(ticket);
            parity(resource->mesh, sources[i].cook());
            const auto& normals = std::get<std::vector<float>>(
                resource->mesh.lods[0].parts[0].find("NORMAL")->values);
            for (std::size_t n = 1; n < normals.size(); n += 3)
                require(normals[n] == 1, "Gallery panels face away from camera");
        }
        EngineContext engine(WorldRole::Runtime);
        Scene scene(engine.world());
        scene.restore_snapshot(authored);
        RuntimeSimulation simulation(engine.world(), scene);
        simulation.reset_presentation();
        const auto render = extract_render_scene(simulation.presentation(1));
        require(render.cameras.size() == 1 && render.meshes.size() == 4,
                "Packed Gallery camera/renderers missing");
        std::set<AssetId> observed;
        for (const auto& actor : render.meshes) {
            const auto found = std::find(meshes.begin(), meshes.end(), actor.renderer.mesh.id);
            if (found == meshes.end())
                continue;
            observed.insert(actor.renderer.mesh.id);
            ResourcePool<MeshAsset> pool;
            const auto ticket = request_model_mesh(pool, root, catalog, actor.renderer.mesh);
            require(pool.wait(ticket, std::chrono::seconds(5)) && bool(pool.acquire(ticket)),
                    "Packed Gallery render resource missing");
            const auto resource = pool.acquire(ticket);
            const auto material = select_mesh_materials(resource.get(), actor.renderer.materials,
                                                        actor.renderer.material_variant);
            require(material.unresolved.empty() && material.bindings.size() == 1 &&
                        material.bindings[0].key == "default",
                    "Packed Gallery checker material slot is unresolved");
            const auto geometry = prepare_mesh_pose_geometry(resource->mesh);
            const auto pose = prepare_mesh_instance_pose(resource.get(), geometry, actor,
                                                         ModelSceneIndex(render));
            const auto& camera = render.cameras[0];
            const auto view = camera_view(camera.camera, camera.world, 1280, 720);
            require(bounds_visible(mesh_instance_bounds(pose, view.position), view),
                    "Packed Gallery panel outside camera");
        }
        require(observed.size() == 3, "Packed Gallery did not use all three Mesh sources");
        const std::array roots{scene.asset_id()};
        {
            ProjectLease lease(root);
            prepare_runtime_content_catalog(lease, roots, {}, scene.schema());
        }
        const auto output =
            root.parent_path() / ("packing-gallery-export-" + AssetId::generate().str());
        package_runtime_content(root, output, roots, {"windows", "d3d12"});
        const auto relocated =
            root.parent_path() / ("packing-gallery-relocated-" + AssetId::generate().str());
        std::filesystem::rename(output, relocated);
        std::filesystem::rename(root, root.parent_path() /
                                          (root.filename().string() + "-source-offline"));
        auto shipped = std::make_shared<const AssetCatalog>(
            open_runtime_content(relocated, {"windows", "d3d12"}));
        for (unsigned i = 0; i < 3; ++i) {
            require(!std::filesystem::exists(relocated / "Assets" / files[i]),
                    "Packed Gallery source leaked into export");
            ResourcePool<MeshAsset> pool;
            const auto ticket = request_model_mesh(pool, relocated, shipped, {meshes[i]});
            require(pool.wait(ticket, std::chrono::seconds(5)) && bool(pool.acquire(ticket)),
                    "Relocated packed Gallery mesh cannot load");
            const auto resource = pool.acquire(ticket);
            parity(resource->mesh, sources[i].cook());
        }
        std::cout << "Gallery44 packing/rotation/source/UV normals/topology/render-consumer and "
                     "source-offline export passed\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
