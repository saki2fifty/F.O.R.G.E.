#include "asset_file_operations.hpp"
#include "asset_reference_impact.hpp"
#include "navigation_behavior_authoring.hpp"
#include "navigation_behavior_resource.hpp"
#include "runtime_package.hpp"
#include "state_graph_layout.hpp"
#include <iostream>

using namespace forge;
using namespace std::chrono_literals;
namespace {
void check(bool value, const std::string& why) {
    if (!value)
        throw std::runtime_error(why);
}
template <class F> void rejects(F run) {
    bool failed = false;
    try {
        run();
    } catch (const std::exception&) {
        failed = true;
    }
    check(failed, "Invalid behavior document edit was admitted");
}
void tree_pipeline(const std::filesystem::path& root,
                   const std::shared_ptr<const ProjectLease>& lease, AssetImportService& imports) {
    auto doc = NavigationBehaviorDocument::create(lease, "Assets/Tree.behavior.json",
                                                  NavigationBehaviorSource::create_tree);
    std::uint32_t parent = 0, move = 0;
    doc->edit(doc->revision(), "Add tree root", [&](Json& d) {
        parent = add_navigation_behavior_node(d, NavigationBehaviorTreeKind::Sequence);
        d["entry"] = parent;
        connect_navigation_behavior_nodes(d, parent, 1);
    });
    const auto before = doc->source().document;
    doc->edit(doc->revision(), "Add tree movement", [&](Json& d) {
        move = add_navigation_behavior_node(d, NavigationBehaviorTreeKind::MoveToPoint);
        d["nodes"].back()["point"] = {2, 0, 3};
        connect_navigation_behavior_nodes(d, parent, move);
    });
    const auto edited = doc->source().document;
    doc->undo();
    check(doc->source().document.at("nodes") == before.at("nodes") &&
              doc->source().document.at("next_id") == edited.at("next_id"),
          "Tree Undo lost node values or monotonic identity");
    doc->redo();
    check(doc->source().document == edited, "Tree Redo did not restore its nodes");
    rejects([&] {
        doc->edit(doc->revision(), "Illegal parent",
                  [&](Json& d) { connect_navigation_behavior_nodes(d, parent, 1); });
    });
    check(doc->source().document == edited, "Rejected tree edit mutated source/history");
    doc->edit(doc->revision(), "Remove tree action",
              [&](Json& d) { remove_navigation_behavior_node(d, move); });
    rejects([&] {
        doc->edit(doc->revision(), "Reuse tree tombstone", [&](Json& d) {
            d["nodes"].push_back(
                {{"id", move}, {"name", "Reused"}, {"kind", "wait"}, {"seconds", 1}});
        });
    });
    doc->undo();
    doc->edit(doc->revision(), "Position tree root", [&](Json& d) {
        asset_detail::set_state_graph_position(d, parent, {120, 70},
                                               navigation_behavior_layout_format);
    });
    doc->save();
    const auto id = doc->source().asset();
    imports.submit(
        imports.prepare(doc->locator(), {}, id),
        [](auto& candidate, const auto& plan, const auto&) {
            prepare_navigation_behavior_publication(candidate, plan);
        },
        [](const auto&, const auto&) {});
    check(imports.wait_idle(10s), "Tree publication timed out");
    const auto outcomes = imports.poll();
    check(outcomes.size() == 1 && outcomes.front().published,
          outcomes.empty() ? "Missing tree publication receipt" : outcomes.front().diagnostic);
    const auto record = AssetCatalog::open_project(root).records().at(id);
    ResourcePool<NavigationBehaviorAsset> pool;
    const auto ticket = request_navigation_behavior(pool, root, record);
    check(pool.wait(ticket, 10s), "Published tree did not load");
    const auto graph = pool.acquire(ticket);
    check(graph && graph->profile == NavigationBehaviorProfile::Tree && graph->nodes.size() == 3,
          "Tree resource lost profile or nodes");
    const auto copied_id = AssetId::generate();
    const auto copied = Json::parse(rewrite_authored_asset(
        record, doc->source().document.dump(), "Assets/TreeCopy.behavior.json", {{id, copied_id}}));
    check(copied.at("asset_id").get<AssetId>() == copied_id &&
              copied.at("nodes") == doc->source().document.at("nodes"),
          "Tree duplication changed stable node identities");
    const auto output = root / "TreeCooked";
    const std::array roots{id};
    (void)package_runtime_content(root, output, roots, {"windows", "d3d12"});
    check(!std::filesystem::exists(output / doc->locator()),
          "Tree export retained editable source");
    std::filesystem::rename(root / doc->locator(), root / "Tree-source-offline");
    const auto moved = root / "TreeRelocated";
    std::filesystem::rename(output, moved);
    const auto catalog = open_runtime_content(moved, {"windows", "d3d12"});
    ResourcePool<NavigationBehaviorAsset> relocated;
    const auto cooked = request_navigation_behavior(relocated, moved, catalog.records().at(id));
    check(relocated.wait(cooked, 10s), "Relocated tree did not load without source");
    const auto loaded = relocated.acquire(cooked);
    check(loaded && loaded->profile == NavigationBehaviorProfile::Tree &&
              select_navigation_behavior_tree(loaded.get(), {}, {}).leaf == 1,
          "Source-free tree did not select its first action");
}
} // namespace
int main(int argc, char** argv) {
    try {
        check(argc == 2, "Need behavior pipeline scratch root");
        const auto root = std::filesystem::absolute(argv[1]) / AssetId::generate().str();
        std::filesystem::create_directories(root / "Assets");
        atomic_write(root / "forge.assets.json",
                     Json{{"version", 1}, {"assets", Json::array()}}.dump());
        const auto lease = std::make_shared<ProjectLease>(root);
        auto doc = NavigationBehaviorDocument::create(lease, "Assets/Patrol.behavior.json");
        const auto initial_revision = doc->revision();
        const auto asset = doc->source().asset();
        doc->edit(initial_revision, "Add patrol", [](Json& d) {
            d["states"].push_back({{"id", 2},
                                   {"name", "Patrol"},
                                   {"action", {{"kind", "move_to_point"}, {"point", {2, 0, 3}}}},
                                   {"transitions", Json::array()}});
            d["states"][0]["transitions"].push_back(
                {{"id", 3}, {"to", 2}, {"condition", {{"kind", "elapsed"}, {"seconds", 1}}}});
            d["next_id"] = 4;
        });
        const auto edited = doc->source().document;
        rejects([&] { doc->edit(initial_revision, "Stale", [](Json&) {}); });
        rejects([&] {
            doc->edit(doc->revision(), "Reuse state as edge", [](Json& d) {
                d["states"].erase(1);
                d["states"][0]["transitions"][0]["id"] = 2;
                d["states"][0]["transitions"][0]["to"] = 1;
            });
        });
        check(doc->source().document == edited, "Rejected role change mutated source/history");
        doc->undo();
        check(doc->source().document.at("states").size() == 1 &&
                  doc->source().document.at("next_id") == 4,
              "Undo rewound the behavior identity allocator");
        doc->redo();
        check(doc->source().document == edited, "Redo did not restore stable graph rows");
        doc->edit(doc->revision(), "Remove patrol", [](Json& d) {
            d["states"].erase(1);
            d["states"][0]["transitions"] = Json::array();
        });
        rejects([&] {
            doc->edit(doc->revision(), "Reuse tombstone", [](Json& d) {
                d["states"][0]["transitions"].push_back(
                    {{"id", 3}, {"to", 1}, {"condition", {{"kind", "arrived"}}}});
            });
        });
        doc->undo();
        doc->save();
        AssetImportService imports(lease, navigation_behavior_import_registry(),
                                   {"windows", "d3d12", "desktop"}, 1);
        const auto publish = [&] {
            imports.submit(
                imports.prepare(doc->locator(), {}, asset),
                [](auto& candidate, const auto& plan, const auto&) {
                    prepare_navigation_behavior_publication(candidate, plan);
                },
                [](const auto&, const auto&) {});
            check(imports.wait_idle(10s), "Behavior publication timed out");
            auto outcomes = imports.poll();
            check(outcomes.size() == 1, "Behavior publication lost its receipt");
            return outcomes.front();
        };
        auto outcome = publish();
        check(outcome.published, outcome.diagnostic);
        const auto first = AssetCatalog::open_project(root).records().at(asset);
        check(first.type == NavigationBehaviorAsset::type && first.dependency_edges.empty(),
              "Behavior publication invented dependencies or changed type");
        ResourcePool<NavigationBehaviorAsset> pool({1, 8, 8, 1024 * 1024});
        const auto first_ticket = request_navigation_behavior(pool, root, first);
        check(pool.wait(first_ticket, 10s), "Published behavior did not load");
        const auto original = pool.acquire(first_ticket);
        check(original && original->asset == asset && original->states.size() == 2,
              "Typed graph lease lost authored identity or states");
        doc->edit(doc->revision(), "Move graph card", [](Json& d) {
            asset_detail::set_state_graph_position(d, 2, {350, 80},
                                                   navigation_behavior_layout_format);
        });
        doc->save();
        outcome = publish();
        check(outcome.published, outcome.diagnostic);
        const auto second = AssetCatalog::open_project(root).records().at(asset);
        const auto copied_id = AssetId::generate();
        const auto copied =
            Json::parse(rewrite_authored_asset(second, doc->source().document.dump(),
                                               "Assets/Copy.behavior.json", {{asset, copied_id}}));
        check(copied.at("asset_id").get<AssetId>() == copied_id &&
                  copied.at("states") == doc->source().document.at("states") &&
                  copied.at("editor_layout") == doc->source().document.at("editor_layout"),
              "Behavior duplicate lost stable rows or layout");
        rejects([&] {
            rewrite_authored_asset(second, doc->source().document.dump(), "Assets/Wrong.json",
                                   {{asset, copied_id}});
        });
        const std::array docs{AssetReferenceDocument{doc->locator(), doc->source().document}};
        const auto refs = collect_asset_references({{"components", Json::array()}}, docs);
        check(refs.references.empty() && refs.uninspected.empty(),
              "Recognized behavior fields invented references/opaque scopes");
        const auto sources = scan_asset_sources(root);
        check(sources.complete &&
                  sources.files.at(doc->locator()).source_kind == NavigationBehaviorAsset::type,
              "Content discovery did not recognize behavior sources");
        DerivedDataCache cache(root);
        const auto artifact = cache.load_selected(
            second.metadata.at("forge.import").at("key").get<std::string>(), [](const auto&) {});
        for (const auto* field :
             {"importer", "importer_revision", "source_digest", "artifact_digest"}) {
            auto forged = second;
            forged.metadata["forge.import"][field] = "unsupported";
            rejects([&] { (void)admit_navigation_behavior_artifact(forged, artifact); });
        }
        const auto package = root / "Cooked";
        const std::array roots{asset};
        (void)package_runtime_content(root, package, roots, {"windows", "d3d12"});
        check(!std::filesystem::exists(package / doc->locator()),
              "Behavior export retained its editable source");
        check(first.metadata.at("forge.import").at("key") !=
                      second.metadata.at("forge.import").at("key") &&
                  first.metadata.at("forge.navigation_behavior") ==
                      second.metadata.at("forge.navigation_behavior"),
              "Layout publication changed cooked semantics or lost source revision");
        const auto second_ticket = request_navigation_behavior(pool, root, second);
        check(pool.wait(second_ticket, 10s) && bool(pool.acquire(second_ticket)) &&
                  original->states.size() == 2,
              "New revision invalidated its retained immutable predecessor");
        const auto source_path = ProjectPaths(root).resolve(doc->locator());
        const auto saved = asset_detail::read_bytes(source_path, navigation_behavior_byte_limit);
        auto invalid = doc->source().document;
        invalid["entry"] = 300;
        atomic_write(source_path, invalid.dump());
        outcome = publish();
        check(!outcome.published && !outcome.diagnostic.empty() &&
                  AssetCatalog::open_project(root).records().at(asset).metadata == second.metadata,
              "Failed source replaced its last-good catalog selection");
        rejects([&] { doc->save(); });
        atomic_write(source_path,
                     std::string(reinterpret_cast<const char*>(saved.data()), saved.size()));
        auto bad = second;
        bad.metadata["forge.import"]["key"] = std::string(64, 'a');
        bad.metadata["forge.import"]["generation"] =
            second.metadata.at("forge.import").at("generation").get<std::uint64_t>() + 1;
        const auto failed = request_navigation_behavior(pool, root, bad);
        check(!pool.wait(failed, 10s) && failed.inspect().state == ResourceState::Failed &&
                  pool.current({asset}).identity().revision ==
                      second_ticket.inspect().identity.revision,
              "Failed preparation retired its last-good graph lease");
        std::filesystem::remove(source_path);
        const auto relocated = root / "Relocated";
        std::filesystem::rename(package, relocated);
        const auto packaged = open_runtime_content(relocated, {"windows", "d3d12"});
        ResourcePool<NavigationBehaviorAsset> packaged_pool;
        const auto packaged_ticket =
            request_navigation_behavior(packaged_pool, relocated, packaged.records().at(asset));
        check(packaged_pool.wait(packaged_ticket, 10s) &&
                  packaged_pool.acquire(packaged_ticket)->states.size() == 2,
              "Relocated source-free behavior export failed typed admission");
        ResourcePool<NavigationBehaviorAsset> cold({1, 8, 8, 1024 * 1024});
        const auto cold_ticket = request_navigation_behavior(cold, root, second);
        check(cold.wait(cold_ticket, 10s) && bool(cold.acquire(cold_ticket)),
              "Cooked-only behavior load consulted its absent editable source");
        tree_pipeline(root, lease, imports);
        std::cout << "Behavior source "
                     "history/identity/import/discovery/copy/references/last-good/relocated cooked "
                     "export PASS\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
