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
            "Triangulation Gallery cooked topology differs");
    for (const auto& field : e.streams)
        require(a.find(field.semantic) && a.find(field.semantic)->values == field.values,
                "Triangulation Gallery cooked stream differs");
}
} // namespace
int main(int argc, char** argv) {
    try {
        require(argc == 3, "Need Gallery and scratch root");
        const auto root = std::filesystem::absolute(argv[2]) / AssetId::generate().str();
        std::filesystem::create_directories(root);
        std::filesystem::copy(argv[1], root, std::filesystem::copy_options::recursive);
        const auto authored = read(root / "Scenes/16-authored-triangulation.scene.json");
        EngineContext engine(WorldRole::Runtime);
        Scene scene(engine.world());
        scene.restore_snapshot(authored);
        auto catalog = std::make_shared<const AssetCatalog>(AssetCatalog::open_project(root));
        const std::array<const char*, 3> files{"PolygonCube.mesh.json", "TriangleCube.mesh.json",
                                               "TriangleSculpt.mesh.json"};
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
                    "Triangulation Gallery source/target differs");
            auto expected = EditableMeshSource::create_cube(meshes[i]);
            const auto original = expected.document;
            if (i) {
                std::vector<std::uint32_t> faces;
                for (const auto& face : expected.document.at("faces"))
                    faces.push_back(face.at("id").get<std::uint32_t>());
                expected.triangulate_faces(faces);
            }
            if (i == 2) {
                const auto point =
                    expected.document["vertices"][6]["position"].get<std::array<double, 3>>();
                const std::array samples{MeshBrushSample{point, 1}};
                expected.sculpt(samples, .35, .18);
                expected.paint_color(samples, .35, {.1, .7, .8, 1}, .9);
            }
            require(sources[i].document == expected.document,
                    "Gallery triangle/source operation differs");
            require(sources[i].document.at("faces").size() == unsigned(i ? 12 : 6),
                    "Gallery authored polygon/triangle count differs");
            if (i < 2)
                require(sources[i].document.at("vertices") == original.at("vertices"),
                        "Triangle conversion moved geometry/colors");
            else
                require(sources[i].document.at("vertices") != original.at("vertices"),
                        "Triangle sculpt sample missing");
            ResourcePool<MeshAsset> pool;
            auto ticket = request_model_mesh(pool, root, catalog, {meshes[i]});
            require(pool.wait(ticket, 5s) && bool(pool.acquire(ticket)),
                    "Triangulation Gallery resource load stalled");
            parity(pool.acquire(ticket)->mesh, sources[i].cook());
        }
        RuntimeSimulation simulation(engine.world(), scene);
        simulation.reset_presentation();
        const auto render = extract_render_scene(simulation.presentation(1));
        require(!render.cameras.empty() && render.meshes.size() == 4,
                std::string("Triangulation Gallery camera/renderers missing: cameras=") +
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
                    "Triangle render consumer unavailable");
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
                    "Triangulation Gallery panel outside game camera");
        }
        require(referenced == std::set<AssetId>(meshes.begin(), meshes.end()),
                "Triangulation Gallery scene references differ");
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
                    "Triangle source leaked into game export");
            ResourcePool<MeshAsset> pool;
            const auto ticket = request_model_mesh(pool, moved, shipped, {meshes[i]});
            require(pool.wait(ticket, 5s) && bool(pool.acquire(ticket)),
                    "Relocated triangle resource unavailable");
            parity(pool.acquire(ticket)->mesh, sources[i].cook());
        }
        require(shipped->records().size() == 4,
                "Triangle scene omitted selected Mesh dependency closure");
        std::cout
            << "Triangulation Gallery topology/seams/source/cook/resource/camera and relocated "
               "source-free export passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
