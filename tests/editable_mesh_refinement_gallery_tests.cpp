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
    require(encode_mesh(actual) == encode_mesh(expected),
            "Refinement resource differs from complete authored cook");
}
} // namespace
int main(int argc, char** argv) {
    try {
        require(argc == 3, "Need Gallery and refinement scratch root");
        const auto root = std::filesystem::absolute(argv[2]) / AssetId::generate().str();
        std::filesystem::create_directories(root);
        std::filesystem::copy(argv[1], root, std::filesystem::copy_options::recursive);
        const auto authored = read(root / "Scenes/46-model-refinement.scene.json");
        const std::array<const char*, 3> files{"ModelRefinement/OriginalCube.mesh.json",
                                               "ModelRefinement/CenterSplitCube.mesh.json",
                                               "ModelRefinement/SubdividedCube.mesh.json"};
        std::array<EditableMeshSource, 3> sources;
        std::array<AssetId, 3> meshes{};
        auto catalog = std::make_shared<const AssetCatalog>(AssetCatalog::open_project(root));
        for (unsigned i = 0; i < 3; ++i) {
            sources[i] = {read(root / "Assets" / files[i])};
            sources[i].validate();
            meshes[i] = sources[i].asset();
            const std::array<unsigned, 3> vertices{8, 14, 26}, faces{6, 24, 48}, next{15, 39, 75};
            require(sources[i].document.at("vertices").size() == vertices[i] &&
                        sources[i].document.at("faces").size() == faces[i] &&
                        sources[i].document.at("next_id") == next[i],
                    "Refinement Gallery source profile differs");
            auto expected = sources[0];
            expected.document["asset_id"] = meshes[i];
            if (i == 1)
                expected.split_faces_at_centers(
                    std::array<std::uint32_t, 6>{9, 10, 11, 12, 13, 14});
            if (i == 2)
                expected.subdivide();
            require(expected.document == sources[i].document,
                    "Gallery source does not match its authored operation");
            const auto& meta = catalog->records().at(meshes[i]).metadata.at("forge.import");
            require(meta.at("platform") == "windows" && meta.at("backend") == "d3d12",
                    "Refinement Gallery target mismatch");
            ResourcePool<MeshAsset> pool;
            const auto ticket = request_model_mesh(pool, root, catalog, {meshes[i]});
            require(pool.wait(ticket, std::chrono::seconds(5)) && bool(pool.acquire(ticket)),
                    "Refinement Gallery mesh load failed");
            const auto resource = pool.acquire(ticket);
            parity(resource->mesh, sources[i].cook());
        }
        EngineContext engine(WorldRole::Runtime);
        Scene scene(engine.world());
        scene.restore_snapshot(authored);
        RuntimeSimulation simulation(engine.world(), scene);
        simulation.reset_presentation();
        const auto render = extract_render_scene(simulation.presentation(1));
        require(render.cameras.size() == 1 && render.meshes.size() == 4,
                "Refinement Gallery camera/renderers missing");
        std::set<AssetId> observed;
        for (const auto& actor : render.meshes) {
            const auto found = std::find(meshes.begin(), meshes.end(), actor.renderer.mesh.id);
            if (found == meshes.end())
                continue;
            observed.insert(actor.renderer.mesh.id);
            ResourcePool<MeshAsset> pool;
            const auto ticket = request_model_mesh(pool, root, catalog, actor.renderer.mesh);
            require(pool.wait(ticket, std::chrono::seconds(5)) && bool(pool.acquire(ticket)),
                    "Refinement Gallery render resource missing");
            const auto resource = pool.acquire(ticket);
            const auto material = select_mesh_materials(resource.get(), actor.renderer.materials,
                                                        actor.renderer.material_variant);
            require(material.unresolved.empty() && material.bindings.size() == 1 &&
                        material.bindings[0].key == "default",
                    "Refinement Gallery checker material slot is unresolved");
            const auto geometry = prepare_mesh_pose_geometry(resource->mesh);
            const auto pose = prepare_mesh_instance_pose(resource.get(), geometry, actor,
                                                         ModelSceneIndex(render));
            const auto& camera = render.cameras[0];
            const auto view = camera_view(camera.camera, camera.world, 1280, 720);
            require(bounds_visible(mesh_instance_bounds(pose, view.position), view),
                    "Refinement Gallery cube outside camera");
        }
        require(observed.size() == 3, "Refinement Gallery did not use all three Mesh sources");
        auto lease = std::make_shared<ProjectLease>(root);
        const std::array roots{scene.asset_id()};
        prepare_runtime_content_catalog(*lease, roots, {}, scene.schema());
        const auto output =
            root.parent_path() / ("refinement-gallery-export-" + AssetId::generate().str());
        package_runtime_content(root, output, roots, {"windows", "d3d12"});
        const auto relocated =
            root.parent_path() / ("refinement-gallery-relocated-" + AssetId::generate().str());
        std::filesystem::rename(output, relocated);
        std::filesystem::rename(root, root.parent_path() /
                                          (root.filename().string() + "-source-offline"));
        auto shipped = std::make_shared<const AssetCatalog>(
            open_runtime_content(relocated, {"windows", "d3d12"}));
        for (unsigned i = 0; i < 3; ++i) {
            require(!std::filesystem::exists(relocated / "Assets" / files[i]),
                    "Refinement Gallery source leaked into export");
            ResourcePool<MeshAsset> pool;
            const auto ticket = request_model_mesh(pool, relocated, shipped, {meshes[i]});
            require(pool.wait(ticket, std::chrono::seconds(5)) && bool(pool.acquire(ticket)),
                    "Relocated refined Gallery mesh cannot load");
            const auto resource = pool.acquire(ticket);
            parity(resource->mesh, sources[i].cook());
        }
        std::cout << "Gallery46 refinement/rotation/source/UV normals/topology/render-consumer and "
                     "source-offline export passed\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
