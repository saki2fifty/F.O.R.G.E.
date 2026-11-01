#include "asset_bytes.hpp"
#include "asset_file_operations.hpp"
#include "asset_import_service.hpp"
#include "asset_reference_impact.hpp"
#include "asset_reimport.hpp"
#include "asset_storage.hpp"
#include "model_render_resource.hpp"
#include "sprite_atlas_importer.hpp"
#include "sprite_atlas_pipeline_fixture.hpp"
#include "sprite_atlas_recipe.hpp"
#include "texture_authoring.hpp"
#include "texture_bundle_validation.hpp"
#include <array>
#include <forge/scene.hpp>
#include <iostream>
#include <source_location>
#include <thread>

using namespace forge;
using namespace forge::asset_detail;
using namespace std::chrono_literals;
namespace {
using test_sprite_atlas_io::image;
using test_sprite_atlas_io::publish;
void require(bool value, const std::string& why) {
    if (!value)
        throw std::runtime_error(why);
}
template <class F> void rejects(F f, std::source_location where = std::source_location::current()) {
    try {
        f();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error("Invalid atlas candidate accepted at line " +
                             std::to_string(where.line()));
}
void published(const AssetImportOutcome& outcome) {
    require(outcome.published, "Atlas pipeline failed: " + outcome.diagnostic);
}
const SpriteFrameRegion& frame(const TextureData& texture, std::uint32_t key) {
    for (const auto& entry : texture.sprite_frames)
        if (entry.id == key)
            return entry.region;
    throw std::runtime_error("Expected atlas frame key is missing");
}
void color(const TextureData& texture, std::uint32_t key, std::array<unsigned char, 3> expected) {
    const auto& r = frame(texture, key);
    const auto offset = (std::size_t(r.y) * texture.width + r.x) * 4;
    for (std::size_t i = 0; i < expected.size(); ++i)
        require(texture.subresources[0].at(offset + i) == std::byte(expected[i]),
                "Frame key was retargeted to another input's pixels");
}
} // namespace
int main(int argc, char** argv) {
    try {
        require(argc == 3, "Need worker executable and fixture root");
        const auto root = std::filesystem::absolute(argv[2]) / AssetId::generate().str();
        std::filesystem::create_directories(root / "Assets");
        AssetCatalog(root).save(AssetCatalog::project_index(root));
        auto lease = std::make_shared<ProjectLease>(root);
        auto registry = texture_import_registry(std::filesystem::absolute(argv[1]));
        const ImportTarget target{"linux", "none", "cpu"};
        AssetImportService service(lease, registry, target);
        const auto red = AssetId::generate(), green = AssetId::generate(),
                   atlas = AssetId::generate();
        image(root / "Assets/Red.tga", 3, 2, {211, 12, 23});
        image(root / "Assets/Green.tga", 2, 4, {8, 199, 34});
        published(publish(service, "Assets/Red.tga", red));
        published(publish(service, "Assets/Green.tga", green));
        const std::array images{red, green};
        auto recipe = SpriteAtlasRecipe::create(atlas, images, {32, 1, 4096, true, true, true});
        recipe.document["frames"][0]["id"] = 41;
        recipe.document["frames"][1]["id"] = 7;
        recipe.document["next_id"] = 42;
        recipe.document["opaque"] = red;
        recipe.document["frames"][0]["opaque"] = green;
        recipe.validate();
        atomic_write(root / "Assets/Characters.atlas.json", recipe.document.dump(2));
        const auto first = publish(service, "Assets/Characters.atlas.json", atlas);
        published(first);
        auto catalog = std::make_shared<const AssetCatalog>(AssetCatalog::open_project(root));
        const auto& record = catalog->records().at(atlas);
        require(record.id == atlas && record.type == TextureAsset::type && !record.subasset &&
                    record.dependency_edges.size() == 2 &&
                    texture_output_profile(record.metadata.at("forge.import")) == 2,
                "Atlas did not retain one Texture identity and two build dependencies");
        require(catalog->dependency_graph().invalidated_by(red) == std::vector<AssetId>{atlas},
                "Atlas did not enter the existing reverse dependency graph");
        ResourcePool<TextureAsset> pool({1, 8, 16, 16 * 1024 * 1024});
        const auto ticket = request_texture(pool, root, catalog, {atlas}, TextureSemantic::Color);
        require(pool.wait(ticket, 5s), "Atlas selected resource did not load");
        const auto retained = pool.acquire(ticket);
        require(retained && retained->sprite_frames.size() == 2 &&
                    retained->sprite_frames[0].id == 7 && retained->sprite_frames[1].id == 41,
                "Sparse frame keys were reordered into positional identities");
        color(retained.get(), 7, {8, 199, 34});
        color(retained.get(), 41, {211, 12, 23});
        const auto old_frames = retained->sprite_frames;
        const auto old_pixels = retained->subresources;
        const auto importer = registry->find(sprite_atlas_importer_id);
        AssetImportRequest request{atlas,
                                   root,
                                   "Assets/Characters.atlas.json",
                                   target,
                                   {std::string(sprite_atlas_importer_id)}};
        const auto captured = importer->discover(request, {});
        require(captured.input.dependencies.size() == 2 && captured.input.output_version == 2,
                "Atlas discovery lost immutable input revisions");
        // Real source size/pixel change: existing watcher must publish the image
        // before its dependent atlas. Blocking an open atlas draft preserves it.
        std::vector<AssetReimportRoute> routes;
        for (const auto& d : registry->descriptors())
            routes.push_back({d.id, target, registry, [](auto& c, const auto& p, const auto&) {
                                  prepare_texture_publication(c, p);
                              }});
        AssetReimportService automatic(lease, std::move(routes), {}, 10min, 10ms);
        std::vector<AssetImportOutcome> outcomes;
        const auto pump = [&] {
            for (auto& result : automatic.poll())
                outcomes.push_back(std::move(result));
        };
        const auto settle = [&] {
            const auto deadline = std::chrono::steady_clock::now() + 20s;
            do {
                pump();
                require(std::chrono::steady_clock::now() < deadline,
                        "Atlas dependent rebuild did not settle");
                std::this_thread::sleep_for(1ms);
            } while (!automatic.complete() || !automatic.settled() || automatic.scanning());
        };
        settle();
        require(outcomes.empty(), "Unchanged atlas was rebuilt at watcher startup");
        bool blocked = true;
        automatic.blocked = [&](AssetId id) { return blocked && id == atlas; };
        image(root / "Assets/Green.tga", 8, 3, {19, 155, 67});
        automatic.rescan();
        const auto deadline = std::chrono::steady_clock::now() + 15s;
        while (outcomes.empty()) {
            pump();
            require(std::chrono::steady_clock::now() < deadline,
                    "Changed atlas input did not rebuild");
            std::this_thread::sleep_for(1ms);
        }
        require(outcomes.size() == 1 && outcomes.front().job.asset == green &&
                    outcomes.front().published,
                "Image dependency did not publish first");
        require(automatic.catalog()->records().at(atlas).metadata == record.metadata,
                "Blocked atlas draft was published");
        rejects([&] { importer->import_and_cook(request, captured, {}, {}); });
        blocked = false;
        settle();
        require(outcomes.size() == 2 && outcomes[1].job.asset == atlas && outcomes[1].published,
                "Existing dependency owner did not rebuild atlas after its image");
        const auto next_ticket =
            request_texture(pool, root, automatic.catalog(), {atlas}, TextureSemantic::Color);
        require(pool.wait(next_ticket, 5s), "Rebuilt atlas resource did not load");
        const auto next = pool.acquire(next_ticket);
        require(next && next->sprite_frames != old_frames && next->subresources != old_pixels,
                "Image size change did not replace complete atlas pixels and mapping");
        color(next.get(), 7, {19, 155, 67});
        color(next.get(), 41, {211, 12, 23});
        require(retained->sprite_frames == old_frames && retained->subresources == old_pixels,
                "Rebuilding atlas mutated a retained immutable pixel/frame lease");
        // Reorder retains identity-to-pixel correspondence; delete plus add never
        // gives an existing Sprite key to a different image.
        const std::array<std::uint32_t, 2> order{7, 41};
        recipe.reorder(order);
        atomic_write(root / request.source, recipe.document.dump());
        published(publish(service, request.source, atlas));
        const auto reordered =
            load_texture_selection(root, AssetCatalog::open_project(root), {atlas});
        color(*reordered.value, 7, {19, 155, 67});
        color(*reordered.value, 41, {211, 12, 23});
        // A failed candidate preserves the entire selected publication, not only
        // its image. No invalid source dependency is published into the graph.
        const auto last_good = AssetCatalog::open_project(root).document();
        recipe.replace_image(7, AssetId::generate());
        atomic_write(root / request.source, recipe.document.dump());
        const auto failed = publish(service, request.source, atlas);
        require(!failed.published && !failed.diagnostic.empty() &&
                    AssetCatalog::open_project(root).document() == last_good,
                "Missing atlas input damaged the last good catalog/publication");
        recipe.replace_image(7, green);
        atomic_write(root / request.source, recipe.document.dump());
        const nlohmann::json schema{{"components", nlohmann::json::array()}};
        const std::array documents{AssetReferenceDocument{request.source, recipe.document}};
        const auto references = inspect_asset_references(schema, documents, {red, green});
        require(references.references.size() == 2 && references.uninspected.size() == 2,
                "Atlas inputs were omitted from reference inspection or opaque UUIDs were guessed");
        const auto rewrite = project_asset_file_rewriter(root, schema);
        rejects([&] {
            rewrite(AssetCatalog::open_project(root).records().at(atlas), recipe.document.dump(),
                    "Assets/Invalid.json", {});
        });
        auto duplicate = prepare_asset_file_operation(
            root, {AssetFileAction::Duplicate, atlas, "Assets/Copy.atlas.json"}, rewrite);
        require(duplicate.result != atlas, "Atlas duplication reused its Texture identity");
        const auto duplicate_id = duplicate.result;
        AssetFileTransaction(*lease).commit(std::move(duplicate.changes), false);
        auto copy = SpriteAtlasRecipe::parse(
            read_bytes(root / "Assets/Copy.atlas.json", sprite_atlas_recipe_byte_limit));
        require(copy.asset() == duplicate_id && copy.inputs() == recipe.inputs() &&
                    copy.next_id() == recipe.next_id() &&
                    copy.document.at("opaque").get<AssetId>() == red &&
                    copy.document.at("frames")[1].at("opaque").get<AssetId>() == green &&
                    !AssetCatalog::open_project(root)
                         .records()
                         .at(duplicate_id)
                         .metadata.contains("forge.import"),
                "Atlas duplication changed keys/external inputs/opaque fields or inherited a "
                "cooked selection");
        published(publish(service, "Assets/Copy.atlas.json", duplicate_id));
        const auto copied_image =
            load_texture_selection(root, AssetCatalog::open_project(root), {duplicate_id});
        color(*copied_image.value, 7, {19, 155, 67});
        color(*copied_image.value, 41, {211, 12, 23});
        auto moved = prepare_asset_file_operation(
            root, {AssetFileAction::Move, duplicate_id, "Assets/Renamed.atlas.json"}, rewrite);
        AssetFileTransaction(*lease).commit(std::move(moved.changes), false);
        require(!std::filesystem::exists(root / "Assets/Copy.atlas.json") &&
                    SpriteAtlasRecipe::parse(read_bytes(root / "Assets/Renamed.atlas.json",
                                                        sprite_atlas_recipe_byte_limit))
                            .asset() == duplicate_id,
                "Atlas relocation changed source identity or retained its old source");
        auto downgraded = record.metadata.at("forge.import");
        downgraded["output_version"] = 1;
        rejects([&] { texture_output_profile(downgraded); });
        downgraded = record.metadata.at("forge.import");
        downgraded["output_version"] = 2.0;
        rejects([&] { texture_output_profile(downgraded); });
        std::cout << "Sprite atlas source capture, real worker publication, dependent rebuilding, "
                     "stable keys and retained leases passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
