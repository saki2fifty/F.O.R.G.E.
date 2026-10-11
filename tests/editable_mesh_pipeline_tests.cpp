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
        {
            auto history = EditableMeshDocument::create(lease, "Assets/Branches.mesh.json");
            const auto baseline = history->source().document;
            history->edit(history->revision(), "Inset", [](nlohmann::json& draft) {
                EditableMeshSource mesh{draft};
                mesh.inset_face(13, .1);
                draft = std::move(mesh.document);
            });
            const auto reserved = history->source().document.at("next_id").get<std::uint32_t>();
            history->undo();
            require(history->source().document.at("vertices") == baseline.at("vertices") &&
                        history->source().document.at("next_id") == reserved,
                    "Undo lowered the Mesh identity allocator");
            history->edit(history->revision(), "Branch recess", [](nlohmann::json& draft) {
                EditableMeshSource mesh{draft};
                mesh.extrude_face(13, -.2);
                draft = std::move(mesh.document);
            });
            for (const auto& vertex : history->source().document.at("vertices")) {
                const auto id = vertex.at("id").get<std::uint32_t>();
                require(id <= 8 || id >= reserved, "New branch reused discarded vertex ID");
            }
            const auto branch_floor = history->source().document.at("next_id");
            history->undo();
            const auto safe = history->source().document;
            const auto revision = history->revision();
            for (bool lower : {true, false}) {
                bool rejected = false;
                try {
                    history->edit(revision, "Invalid identity", [&](nlohmann::json& draft) {
                        if (lower)
                            draft["next_id"] = 15;
                        else
                            draft["vertices"].push_back(
                                {{"id", 15}, {"position", std::array<double, 3>{0, 0, 0}}});
                    });
                } catch (const std::runtime_error&) {
                    rejected = true;
                }
                require(rejected && history->revision() == revision &&
                            history->source().document == safe && history->can_redo(),
                        "Invalid allocator edit changed history/source");
            }
            history->save();
            EditableMeshDocument reopened_history(lease, "Assets/Branches.mesh.json");
            require(reopened_history.source().document.at("next_id") == branch_floor,
                    "Save/reopen lost reserved Mesh identities");
            history->redo();
            require(history->source().document.at("next_id") == branch_floor &&
                        history->source().document.at("vertices").size() == 12,
                    "Redo lost geometry or identity high water");
        }
        {
            auto uv_history = EditableMeshDocument::create(lease, "Assets/UvHistory.mesh.json");
            const auto baseline = uv_history->source().document;
            auto operation = [](nlohmann::json& draft) {
                EditableMeshSource mesh{draft};
                const auto island = mesh.uv_island(13);
                mesh.transform_uv(island, {.3, -.2}, .4, {-2, .5});
                mesh.fit_uv(island, .1);
                draft = std::move(mesh.document);
            };
            uv_history->edit(uv_history->revision(), "UV island", operation);
            const auto edited = uv_history->source().document;
            require(edited != baseline && edited.at("next_id") == baseline.at("next_id"),
                    "UV-only history failed or allocated geometry identities");
            uv_history->undo();
            require(uv_history->source().document == baseline && uv_history->can_redo(),
                    "UV Undo did not restore its source");
            uv_history->redo();
            require(uv_history->source().document == edited, "UV Redo differs");
            uv_history->undo();
            uv_history->edit(uv_history->revision(), "UV branch", operation);
            require(!uv_history->can_redo() && uv_history->source().document == edited,
                    "UV branch retained abandoned history");
            uv_history->save();
            EditableMeshDocument reopened_uv(lease, "Assets/UvHistory.mesh.json");
            require(reopened_uv.source().document == edited, "UV source save/reopen differs");
        }
        {
            auto history = EditableMeshDocument::create(
                lease, "Assets/MirrorHistory.mesh.json",
                [](AssetId id) { return EditableMeshSource::create_sculpt_sphere(id); });
            const auto baseline = history->source().document;
            auto sculpt = [](Json& draft) {
                EditableMeshSource mesh{draft};
                mesh.sculpt(std::array{MeshBrushSample{{.5, 0, 0}, .8}}, .35, -.02,
                            MeshBrushSymmetry::X);
                draft = std::move(mesh.document);
            };
            history->edit(history->revision(), "Mirrored inward stroke", sculpt);
            const auto sculpted = history->source().document;
            history->undo();
            require(history->source().document == baseline, "Mirror Undo differs");
            history->redo();
            require(history->source().document == sculpted, "Mirror Redo differs");
            history->undo();
            history->edit(history->revision(), "Mirrored paint branch", [](Json& draft) {
                EditableMeshSource mesh{draft};
                mesh.paint_color(
                    std::array{MeshBrushSample{{.35, .2, .3}, .8}}, .35, {.1, .8, .2, 1}, .7,
                    MeshBrushSymmetry::X | MeshBrushSymmetry::Y | MeshBrushSymmetry::Z);
                draft = std::move(mesh.document);
            });
            require(!history->can_redo(), "Brush branch retained abandoned redo");
            const auto painted = history->source().document;
            history->save();
            EditableMeshDocument reopened(lease, "Assets/MirrorHistory.mesh.json");
            require(reopened.source().document == painted, "Mirror reopen differs");
            AssetImportService brush_service(lease, editable_mesh_import_registry(),
                                             {"windows", "d3d12", "desktop"});
            require(
                publish(brush_service, "Assets/MirrorHistory.mesh.json", reopened.source().asset())
                    .published,
                "Mirrored brush publication failed");
            const auto selected =
                std::make_shared<const AssetCatalog>(AssetCatalog::open_project(root));
            ResourcePool<MeshAsset> pool;
            auto ticket = request_model_mesh(pool, root, selected, {reopened.source().asset()});
            require(pool.wait(ticket, 5s) && bool(pool.acquire(ticket)),
                    "Published brush resource unavailable");
            const auto expected = reopened.source().cook();
            const auto& actual = pool.acquire(ticket)->mesh;
            require(actual.lods[0].parts[0].indices == expected.lods[0].parts[0].indices,
                    "Brush cooked indices differ");
            for (const auto& stream : expected.lods[0].parts[0].streams)
                require(actual.lods[0].parts[0].find(stream.semantic) &&
                            actual.lods[0].parts[0].find(stream.semantic)->values == stream.values,
                        "Published brush stream differs");
        }
        {
            auto history = EditableMeshDocument::create(lease, "Assets/TriangleHistory.mesh.json");
            const auto baseline = history->source().document;
            history->edit(history->revision(), "Triangulate face", [](Json& draft) {
                EditableMeshSource mesh{draft};
                mesh.triangulate_faces(std::array<std::uint32_t, 1>{13});
                draft = std::move(mesh.document);
            });
            const auto edited = history->source().document;
            history->undo();
            auto reserved = baseline;
            reserved["next_id"] = edited.at("next_id");
            require(history->source().document == reserved,
                    "Triangle Undo lost geometry or allocator");
            history->redo();
            require(history->source().document == edited, "Triangle Redo differs");
            history->undo();
            history->edit(history->revision(), "Triangle branch", [](Json& draft) {
                EditableMeshSource mesh{draft};
                mesh.triangulate_faces(std::array<std::uint32_t, 1>{9});
                draft = std::move(mesh.document);
            });
            require(!history->can_redo() && history->source().document.at("next_id") == 17,
                    "Triangle branch reused discarded ID");
            history->save();
            EditableMeshDocument reopened(lease, "Assets/TriangleHistory.mesh.json");
            require(reopened.source().document == history->source().document,
                    "Triangle save/reopen differs");
        }
        {
            auto shading = EditableMeshDocument::create(lease, "Assets/ShadingHistory.mesh.json");
            const auto baseline = shading->source().document;
            auto revision = shading->revision();
            shading->edit(revision, "Flat no-op", [](Json& draft) {
                EditableMeshSource mesh{draft};
                mesh.set_smooth_shading(false);
                draft = std::move(mesh.document);
            });
            require(shading->revision() == revision && !shading->can_undo(),
                    "Effective flat no-op changed history");
            shading->edit(revision, "Smooth", [](Json& draft) {
                EditableMeshSource mesh{draft};
                mesh.set_smooth_shading(true);
                draft = std::move(mesh.document);
            });
            const auto smooth = shading->source().document;
            shading->undo();
            require(shading->source().document == baseline, "Shading Undo differs");
            shading->redo();
            require(shading->source().document == smooth, "Shading Redo differs");
            shading->save();
            EditableMeshDocument reopened(lease, "Assets/ShadingHistory.mesh.json");
            require(reopened.source().document == smooth, "Shading source save/reopen differs");
        }
        auto document = EditableMeshDocument::create(lease, source);
        const auto identity = document->source().asset();
        const auto initial = document->source().document;
        document->edit(document->revision(), "Inset and recess top", [](nlohmann::json& draft) {
            EditableMeshSource mesh{draft};
            mesh.inset_face(13, .1);
            const auto cap = mesh.extrude_face(13, -.2);
            mesh.transform_uv(cap, std::array<std::size_t, 4>{0, 1, 2, 3}, {0.25, 0.125}, 0.0,
                              0.75);
            const auto point =
                mesh.document["vertices"][0]["position"].get<std::array<double, 3>>();
            mesh.paint_color(std::array{MeshBrushSample{point, 1}}, .2, {1, 0, 0, 1}, 1);
            draft = std::move(mesh.document);
        });
        const auto extruded = document->source().document;
        require(document->dirty() && document->can_undo(),
                "Editable Mesh edit did not enter source history");
        document->undo();
        auto restored = initial;
        restored["next_id"] = extruded.at("next_id");
        require(document->source().document == restored && document->dirty() &&
                    document->can_redo(),
                "One Undo did not restore the original Mesh and UVs");
        document->redo();
        require(document->source().document == extruded && document->dirty(),
                "Redo did not restore the extrusion and UV change together");
        const auto revision = document->revision();
        bool rejected = false;
        try {
            document->edit(revision, "Invalid transform", [](nlohmann::json& draft) {
                EditableMeshSource mesh{draft};
                mesh.transform_vertices(std::array<std::uint32_t, 1>{1}, {}, {}, {0, 1, 1});
                draft = std::move(mesh.document);
            });
        } catch (const std::runtime_error&) {
            rejected = true;
        }
        require(rejected, "Invalid Mesh edit unexpectedly succeeded");
        require(document->revision() == revision && document->source().document == extruded,
                "Rejected Mesh edit changed source or history revision");
        document->save();
        EditableMeshDocument reopened(lease, source);
        require(reopened.source().document == document->source().document,
                "Reopened editable Mesh lost its authored geometry or UVs");
        const auto authored_mesh = reopened.source().cook();
        const auto authored_uv =
            std::get<std::vector<float>>(authored_mesh.lods[0].parts[0].find("TEXCOORD_0")->values);
        const auto authored_color =
            std::get<std::vector<float>>(authored_mesh.lods[0].parts[0].find("COLOR_0")->values);
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
        require(geometry && geometry->mesh.lods[0].parts[0].indices.size() == 84,
                "Published geometry differs from authored extrusion");
        require(std::get<std::vector<float>>(
                    geometry->mesh.lods[0].parts[0].find("TEXCOORD_0")->values) == authored_uv,
                "Published Mesh lost face-corner UV edits");
        require(std::get<std::vector<float>>(
                    geometry->mesh.lods[0].parts[0].find("COLOR_0")->values) == authored_color,
                "Published Mesh lost painted vertex colors");
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
        require(std::get<std::vector<float>>(shipped_pool.acquire(shipped_ticket)
                                                 ->mesh.lods[0]
                                                 .parts[0]
                                                 .find("COLOR_0")
                                                 ->values) == authored_color,
                "Relocated standalone content lost painted vertex colors");
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
