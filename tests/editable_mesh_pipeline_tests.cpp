#include "editable_mesh_authoring.hpp"
#include "editable_mesh_document.hpp"
#include "model_render_resource.hpp"
#include "runtime_package.hpp"
#include <fstream>
#include <iostream>

using namespace forge;
using namespace forge::asset_detail;
using namespace std::chrono_literals;
namespace {
void require(bool value, const std::string& why) {
    if (!value)
        throw std::runtime_error(why);
}
AssetImportOutcome publish(AssetImportService& service, const std::filesystem::path& locator,
                           AssetId identity) {
    auto draft = service.prepare(locator, {}, identity);
    service.submit(
        std::move(draft),
        [](auto& candidate, const auto& plan, const auto&) {
            prepare_editable_mesh_publication(candidate, plan);
        },
        [](const auto&, const auto&) {});
    require(service.wait_idle(10s), "Editable Mesh import stalled");
    auto results = service.poll();
    require(results.size() == 1, "Editable Mesh import receipt missing");
    return std::move(results.front());
}
} // namespace
int main(int argc, char** argv) {
    try {
        require(argc == 2, "Need editable Mesh scratch directory");
        const auto root = std::filesystem::absolute(argv[1]) / AssetId::generate().str();
        std::filesystem::create_directories(root / "Assets");
        auto lease = std::make_shared<ProjectLease>(root);
        const auto source = std::filesystem::path("Assets/Shape.mesh.json");
        auto document = EditableMeshDocument::create(lease, source);
        const auto identity = document->source().asset();
        document->edit(document->revision(), "Extrude top", [](nlohmann::json& draft) {
            EditableMeshSource mesh{draft};
            const auto cap = mesh.extrude_face(13, .5);
            mesh.transform_uv(cap, std::array<std::size_t, 4>{0, 1, 2, 3}, {0.25, 0.125}, 0.0,
                              0.75);
            draft = std::move(mesh.document);
        });
        document->save();
        EditableMeshDocument reopened(lease, source);
        require(reopened.source().document == document->source().document,
                "Reopened editable Mesh lost its authored geometry or UVs");
        const auto authored_uv = std::get<std::vector<float>>(
            reopened.source().cook().lods[0].parts[0].find("TEXCOORD_0")->values);
        AssetImportService service(lease, editable_mesh_import_registry(),
                                   {"linux", "none", "cpu"});
        auto result = publish(service, source, identity);
        require(result.published, "Editable Mesh publication failed: " + result.diagnostic);
        auto catalog = std::make_shared<const AssetCatalog>(AssetCatalog::open_project(root));
        require(catalog->records().at(identity).type == MeshAsset::type &&
                    !catalog->records().at(identity).subasset,
                "Published editable Mesh lacks root Mesh identity");
        ResourcePool<MeshAsset> pool;
        auto ticket = request_model_mesh(pool, root, catalog, {identity});
        require(pool.wait(ticket, 5s),
                "Published editable Mesh did not enter shared resource pool");
        auto geometry = pool.acquire(ticket);
        require(geometry && geometry->mesh.lods[0].parts[0].indices.size() == 60,
                "Published geometry differs from authored extrusion");
        require(std::get<std::vector<float>>(
                    geometry->mesh.lods[0].parts[0].find("TEXCOORD_0")->values) == authored_uv,
                "Published Mesh lost face-corner UV edits");
        const auto package = root / "standalone-content";
        const std::array roots{identity};
        (void)package_runtime_content(root, package, roots, {"linux", "none"});
        require(!std::filesystem::exists(package / source),
                "Editable Mesh authoring source leaked into standalone content");
        auto shipped =
            std::make_shared<const AssetCatalog>(open_runtime_content(package, {"linux", "none"}));
        ResourcePool<MeshAsset> shipped_pool;
        auto shipped_ticket = request_model_mesh(shipped_pool, package, shipped, {identity});
        require(shipped_pool.wait(shipped_ticket, 5s) && bool(shipped_pool.acquire(shipped_ticket)),
                "Relocated standalone content cannot load cooked editable Mesh");
        require(std::get<std::vector<float>>(shipped_pool.acquire(shipped_ticket)
                                                 ->mesh.lods[0]
                                                 .parts[0]
                                                 .find("TEXCOORD_0")
                                                 ->values) == authored_uv,
                "Relocated standalone content lost authored UV placement");
        const auto prior_key =
            catalog->records().at(identity).metadata.at("forge.import").at("key");
        {
            std::ofstream out(root / source, std::ios::trunc);
            out << "{ invalid mesh";
        }
        try {
            auto rejected = publish(service, source, identity);
            require(!rejected.published, "Invalid editable Mesh replaced last good revision");
        } catch (const std::exception&) {
            // Invalid source may be rejected before a candidate is queued.
        }
        auto retained = AssetCatalog::open_project(root);
        require(retained.records().at(identity).metadata.at("forge.import").at("key") == prior_key,
                "Rejected editable Mesh changed selected catalog revision");
        require(pool.current({identity}).identity() == geometry.identity(),
                "Rejected editable Mesh displaced last-good runtime resource");
        std::cout << "editable Mesh publication/resource tests passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
