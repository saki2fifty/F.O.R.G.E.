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
            "Facing Gallery cooked topology differs");
    for (const auto& field : e.streams)
        require(a.find(field.semantic) && a.find(field.semantic)->values == field.values,
                "Facing Gallery cooked stream differs");
}
} // namespace
int main(int argc, char** argv) {
    try {
        require(argc == 3, "Need Gallery and scratch root");
        const auto root = std::filesystem::absolute(argv[2]) / AssetId::generate().str();
        std::filesystem::create_directories(root);
        std::filesystem::copy(argv[1], root, std::filesystem::copy_options::recursive);
        const auto authored = read(root / "Scenes/19-mesh-facing.scene.json");
        EngineContext engine(WorldRole::Runtime);
        Scene scene(engine.world());
        scene.restore_snapshot(authored);
        auto catalog = std::make_shared<const AssetCatalog>(AssetCatalog::open_project(root));
        const std::array<const char*, 2> files{"OutwardRoom.mesh.json", "InwardRoom.mesh.json"};
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
                    "Facing Gallery source/target differs");
            require(!sources[i].document.value("smooth", false) &&
                        sources[i].document.at("vertices").size() == 8 &&
                        sources[i].document.at("faces").size() == 5 &&
                        sources[i].document.at("next_id") == 15,
                    "Gallery room source/shading/allocator differs");
            ResourcePool<MeshAsset> pool;
            auto ticket = request_model_mesh(pool, root, catalog, {meshes[i]});
            require(pool.wait(ticket, 5s) && bool(pool.acquire(ticket)),
                    "Facing Gallery resource load stalled");
            parity(pool.acquire(ticket)->mesh, sources[i].cook());
        }
        auto expected = sources[0].document;
        expected["asset_id"] = sources[1].asset();
        for (auto& face : expected.at("faces")) {
            const auto corners = face.at("corners");
            for (std::size_t i = 1; i < corners.size(); ++i)
                face["corners"][i] = corners[corners.size() - i];
        }
        require(expected == sources[1].document,
                "Facing rooms differ in geometry/UV/complete corner data/face or vertex IDs");
        for (unsigned side = 0; side < 2; ++side) {
            const auto mesh = sources[side].cook();
            const auto& part = mesh.lods[0].parts[0];
            const auto& positions = std::get<std::vector<float>>(part.find("POSITION")->values);
            const auto& normals = std::get<std::vector<float>>(part.find("NORMAL")->values);
            require(part.indices.size() == 30, "Open-front room did not cook to ten triangles");
            for (unsigned row = 0; row < part.vertices; ++row) {
                double signed_distance = 0;
                for (unsigned axis = 0; axis < 3; ++axis)
                    signed_distance += positions[row * 3 + axis] * normals[row * 3 + axis];
                require(side ? signed_distance < -.49 : signed_distance > .49,
                        "Room normal does not point toward/away from the interior");
            }
        }
        RuntimeSimulation simulation(engine.world(), scene);
        simulation.reset_presentation();
        const auto render = extract_render_scene(simulation.presentation(1));
        require(!render.cameras.empty() && render.meshes.size() == 3,
                std::string("Facing Gallery camera/renderers missing: cameras=") +
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
                    "Facing render consumer unavailable");
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
                    "Facing Gallery panel outside game camera");
        }
        require(referenced == std::set<AssetId>(meshes.begin(), meshes.end()),
                "Facing Gallery scene references differ");
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
                    "Facing source leaked into game export");
            ResourcePool<MeshAsset> pool;
            const auto ticket = request_model_mesh(pool, moved, shipped, {meshes[i]});
            require(pool.wait(ticket, 5s) && bool(pool.acquire(ticket)),
                    "Relocated facing resource unavailable");
            parity(pool.acquire(ticket)->mesh, sources[i].cook());
        }
        require(shipped->records().size() == 3,
                "Facing scene omitted selected Mesh dependency closure");
        std::cout << "Facing Gallery topology/seams/source/cook/resource/camera and relocated "
                     "source-free export passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
