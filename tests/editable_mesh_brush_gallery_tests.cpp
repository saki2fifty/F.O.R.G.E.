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
            "Brush Gallery cooked topology differs");
    for (const auto& field : e.streams)
        require(a.find(field.semantic) && a.find(field.semantic)->values == field.values,
                "Brush Gallery cooked stream differs");
}
} // namespace
int main(int argc, char** argv) {
    try {
        require(argc == 3, "Need Gallery and scratch root");
        const auto root = std::filesystem::absolute(argv[2]) / AssetId::generate().str();
        std::filesystem::create_directories(root);
        std::filesystem::copy(argv[1], root, std::filesystem::copy_options::recursive);
        const auto authored = read(root / "Scenes/15-brush-symmetry.scene.json");
        EngineContext engine(WorldRole::Runtime);
        Scene scene(engine.world());
        scene.restore_snapshot(authored);
        auto catalog = std::make_shared<const AssetCatalog>(AssetCatalog::open_project(root));
        const std::array<const char*, 3> files{
            "SculptOneSide.mesh.json", "SculptMirrored.mesh.json", "PaintMirrored.mesh.json"};
        std::array<EditableMeshSource, 3> sources;
        std::array<AssetId, 3> meshes{};
        for (unsigned i = 0; i < 3; ++i) {
            sources[i] = {read(root / "Assets" / files[i])};
            sources[i].validate();
            meshes[i] = sources[i].asset();
            const auto& selected = catalog->records().at(meshes[i]);
            const auto& meta = selected.metadata.at("forge.import");
            require(selected.source == std::filesystem::path("Assets") / files[i] &&
                        meta.at("platform") == "windows" && meta.at("backend") == "d3d12",
                    "Brush Gallery source/target differs");
            auto expected = EditableMeshSource::create_sculpt_sphere(meshes[i]);
            const auto original = expected.document;
            if (i < 2) {
                const std::array one{MeshBrushSample{{.5, 0, 0}, 1}};
                const std::array both{MeshBrushSample{{.5, 0, 0}, 1},
                                      MeshBrushSample{{-.5, 0, 0}, 1}};
                if (i == 0)
                    expected.sculpt(one, .35, .1);
                else
                    expected.sculpt(both, .35, .1);
            } else {
                std::vector<MeshBrushSample> reflected;
                for (unsigned mask = 0; mask < 8; ++mask) {
                    std::array<double, 3> point{.35, .2, .3};
                    for (unsigned axis = 0; axis < 3; ++axis)
                        if (mask & (1u << axis))
                            point[axis] = -point[axis];
                    reflected.push_back({point, 1});
                }
                expected.paint_color(reflected, .35, {.9, .2, .1, 1}, .9);
            }
            require(sources[i].document == expected.document,
                    "Gallery authored brush differs from independent explicit-stamp oracle");
            require(sources[i].document.at("faces") == original.at("faces") &&
                        sources[i].document.at("next_id") == original.at("next_id"),
                    "Brush Gallery changed topology/UVs/IDs");
            ResourcePool<MeshAsset> pool;
            auto ticket = request_model_mesh(pool, root, catalog, {meshes[i]});
            require(pool.wait(ticket, 5s) && bool(pool.acquire(ticket)),
                    "Brush Gallery resource load stalled");
            parity(pool.acquire(ticket)->mesh, sources[i].cook());
        }
        RuntimeSimulation simulation(engine.world(), scene);
        simulation.reset_presentation();
        const auto render = extract_render_scene(simulation.presentation(1));
        require(!render.cameras.empty() && render.meshes.size() == 4,
                std::string("Brush Gallery camera/renderers missing: cameras=") +
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
                    "Brush render consumer unavailable");
            const auto loaded = pool.acquire(ticket);
            const auto& resource = loaded.get();
            const auto geometry = prepare_mesh_pose_geometry(resource.mesh);
            const auto pose =
                prepare_mesh_instance_pose(resource, geometry, mesh, ModelSceneIndex(render));
            const auto& camera = render.cameras.front();
            const auto view = camera_view(camera.camera, camera.world, 1280, 720);
            require(bounds_visible(mesh_instance_bounds(pose, view.position), view),
                    "Brush Gallery panel outside game camera");
        }
        require(referenced == std::set<AssetId>(meshes.begin(), meshes.end()),
                "Brush Gallery scene references differ");
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
        for (unsigned i = 0; i < 3; ++i) {
            require(!std::filesystem::exists(moved / "Assets" / files[i]),
                    "Brush source leaked into game export");
            ResourcePool<MeshAsset> pool;
            const auto ticket = request_model_mesh(pool, moved, shipped, {meshes[i]});
            require(pool.wait(ticket, 5s) && bool(pool.acquire(ticket)),
                    "Relocated brush resource unavailable");
            parity(pool.acquire(ticket)->mesh, sources[i].cook());
        }
        require(shipped->records().size() == 4,
                "Brush scene omitted selected Mesh dependency closure");
        std::cout << "Brush Gallery topology/seams/source/cook/resource/camera and relocated "
                     "source-free export passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
