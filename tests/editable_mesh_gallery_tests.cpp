#include "model_render_resource.hpp"
#include "runtime_package.hpp"
#include <forge/authoring.hpp>
#include <forge/editable_mesh.hpp>
#include <fstream>
#include <iostream>
using namespace forge;
using namespace forge::asset_detail;
using namespace std::chrono_literals;
namespace {
void require(bool ok, const char* why) {
    if (!ok)
        throw std::runtime_error(why);
}
Json read(const std::filesystem::path& p) {
    Json j;
    std::ifstream(p) >> j;
    return j;
}
} // namespace
int main(int argc, char** argv) {
    try {
        require(argc == 3, "Need Gallery and scratch root");
        const auto root = std::filesystem::absolute(argv[2]) / AssetId::generate().str();
        std::filesystem::create_directories(root);
        std::filesystem::copy(argv[1], root, std::filesystem::copy_options::recursive);
        const auto authored = read(root / "Scenes/12-inset-panels.scene.json");
        EngineContext engine;
        Scene scene(engine.world());
        scene.restore_snapshot(authored);
        require(scene.entity_count() >= 6, "Panel Gallery scene incomplete");
        auto catalog = std::make_shared<const AssetCatalog>(AssetCatalog::open_project(root));
        const std::array<const char*, 3> files{"Frame.mesh.json", "RaisedPanel.mesh.json",
                                               "RecessedPanel.mesh.json"};
        const std::array<double, 3> top{.225, .575, .225};
        std::vector<AssetId> meshes;
        for (unsigned i = 0; i < 3; ++i) {
            EditableMeshSource source{read(root / "Assets" / files[i])};
            source.validate();
            const auto id = source.asset();
            meshes.push_back(id);
            const auto& record = catalog->records().at(id);
            require(record.type == MeshAsset::type &&
                        record.source == std::filesystem::path("Assets") / files[i],
                    "Panel source/catalog identity differs");
            const auto imported = record.metadata.at("forge.import");
            require(imported.at("platform") == "windows" && imported.at("backend") == "d3d12",
                    "Gallery panel has wrong cook target");
            auto cooked = source.cook();
            const auto& part = cooked.lods[0].parts[0];
            require(part.indices.size() == (i == 0 ? 60 : 84), "Panel topology differs");
            const auto& positions = std::get<std::vector<float>>(part.find("POSITION")->values);
            double max_y = -100, min_y = 100;
            for (std::size_t v = 1; v < positions.size(); v += 3) {
                max_y = std::max(max_y, double(positions[v]));
                min_y = std::min(min_y, double(positions[v]));
            }
            require(std::abs(max_y - top[i]) < 1e-6 && std::abs(min_y + .225) < 1e-6,
                    "Panel exterior depth differs");
            if (i == 2) {
                bool recessed = false;
                for (const auto& v : source.document.at("vertices"))
                    recessed |= std::abs(v.at("position")[1].get<double>() - .075) < 1e-8;
                require(recessed, "Panel recess has no inward cap");
            }
            ResourcePool<MeshAsset> pool;
            auto ticket = request_model_mesh(pool, root, catalog, {id});
            require(pool.wait(ticket, 5s), "Gallery panel resource stalled");
            auto loaded = pool.acquire(ticket);
            require(bool(loaded), "Gallery panel resource unavailable");
            const auto& actual = loaded->mesh.lods[0].parts[0];
            require(actual.indices == part.indices && actual.streams.size() == part.streams.size(),
                    "Published panel topology differs");
            for (const auto& attribute : part.streams)
                require(actual.find(attribute.semantic) &&
                            actual.find(attribute.semantic)->values == attribute.values,
                        "Published panel attribute differs");
        }
        std::set<AssetId> referenced;
        for (const auto& row : authored.at("entities"))
            if (row.at("components").contains("forge.mesh_renderer")) {
                const auto& mesh = row.at("components").at("forge.mesh_renderer");
                referenced.insert(mesh.at("mesh").get<AssetId>());
                require(mesh.at("materials").size() == 1, "Panel lacks checker material binding");
            }
        require(referenced == std::set<AssetId>(meshes.begin(), meshes.end()),
                "Scene panel references differ");
        const std::array roots{authored.at("asset_id").get<AssetId>()};
        auto lease = std::make_shared<ProjectLease>(root);
        prepare_runtime_content_catalog(*lease, roots, {});
        const auto packaged = root.parent_path() / (root.filename().string() + "-export");
        (void)package_runtime_content(root, packaged, roots, {"windows", "d3d12"});
        const auto moved = std::filesystem::path(packaged.string() + "-relocated");
        std::filesystem::rename(packaged, moved);
        auto shipped =
            std::make_shared<const AssetCatalog>(open_runtime_content(moved, {"windows", "d3d12"}));
        for (unsigned i = 0; i < 3; ++i) {
            require(!std::filesystem::exists(moved / "Assets" / files[i]),
                    "Modeling source leaked into game export");
            ResourcePool<MeshAsset> pool;
            auto ticket = request_model_mesh(pool, moved, shipped, {meshes[i]});
            require(pool.wait(ticket, 5s) && bool(pool.acquire(ticket)),
                    "Relocated game cannot load panel");
            const auto source = EditableMeshSource{read(root / "Assets" / files[i])}.cook();
            const auto& actual = pool.acquire(ticket)->mesh.lods[0].parts[0];
            for (const auto& attribute : source.lods[0].parts[0].streams)
                require(actual.find(attribute.semantic)->values == attribute.values,
                        "Relocated panel lost UV/color/normals/positions");
        }
        require(shipped->records().size() >= 5,
                "Scene export omitted panel material dependency closure");
        std::cout << "Gallery panel source/cook/resource and relocated source-free export passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
