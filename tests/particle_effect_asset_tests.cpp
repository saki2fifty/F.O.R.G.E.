#include "asset_file_operations.hpp"
#include "asset_reference_impact.hpp"
#include "asset_storage.hpp"
#include "particle_effect_authoring.hpp"
#include "particle_effect_resource.hpp"
#include "runtime_package.hpp"
#include <forge/asset_discovery.hpp>
#include <forge/engine_assets.hpp>
#include <iostream>
using namespace forge;
using namespace std::chrono_literals;
namespace {
void require(bool value, const std::string& message) {
    if (!value)
        throw std::runtime_error(message);
}
template <class F> void reject(F&& action) {
    try {
        action();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error("Invalid particle asset operation accepted");
}
AssetImportOutcome publish(AssetImportService& service, const std::filesystem::path& source,
                           AssetId id) {
    service.submit(
        service.prepare(source, {}, id),
        [](auto& candidate, const auto& plan, const auto&) {
            prepare_particle_effect_publication(candidate, plan);
        },
        [](const auto&, const auto&) {});
    require(service.wait_idle(10s), "Particle effect import timed out");
    auto results = service.poll();
    require(results.size() == 1, "Particle effect publication receipt missing");
    return std::move(results.front());
}
ParticleEffectData load(const std::filesystem::path& root,
                        std::shared_ptr<const AssetCatalog> catalog, AssetId id) {
    ResourcePool<ParticleEffectAsset> pool({1, 4, 8, 64 * 1024});
    const auto ticket = request_particle_effect(pool, root, std::move(catalog), {id});
    require(pool.wait(ticket, 10s), "Particle Effect load failed: " + ticket.inspect().diagnostic);
    const auto lease = pool.acquire(ticket);
    require(bool(lease), "Particle Effect lease missing");
    return lease.get();
}
} // namespace
int main(int argc, char** argv) {
    try {
        require(argc == 2, "Need particle asset scratch root");
        const auto root = std::filesystem::absolute(argv[1]) / AssetId::generate().str();
        std::filesystem::create_directories(root / "Assets");
        auto lease = std::make_shared<ProjectLease>(root);
        auto document = ParticleEffectDocument::create(lease, "Assets/Sparks.particles.json");
        const auto id = document->source().asset();
        const auto initial = document->source().document;
        document->edit(document->revision(), "Particle cone and metadata", [](auto& value) {
            value["cone_angle"] = 35;
            value["extensions"] = {{"author-note", {{"label", "original fixture"}}}};
        });
        const auto changed = document->source().document;
        const auto revision = document->revision();
        reject([&] {
            document->edit(revision - 1, "Stale", [](auto& value) { value["spawn_rate"] = 1; });
        });
        reject([&] {
            document->edit(revision, "Over capacity", [](auto& value) { value["capacity"] = 1; });
        });
        require(document->revision() == revision && document->source().document == changed,
                "Rejected edit mutated particle source/history");
        document->undo();
        require(document->source().document == initial && !document->dirty(),
                "Particle Undo differs from initial source");
        document->redo();
        require(document->source().document == changed, "Particle Redo lost extension payload");
        document->save();
        ParticleEffectDocument reopened(lease, document->locator());
        require(reopened.source().document == changed, "Particle saved source/reopen mismatch");
        require(decode_cooked_particle_effect(document->source().cook(), id) ==
                    document->source().compiled(),
                "Particle cooked projection differs from source values");
        reject(
            [&] { decode_cooked_particle_effect(document->source().cook(), AssetId::generate()); });
        AssetImportService service(lease, particle_effect_import_registry(),
                                   {"linux", "none", "cpu"});
        auto first = publish(service, document->locator(), id);
        require(first.published && !first.cache_hit,
                "First particle publication failed: " + first.diagnostic);
        auto catalog = std::make_shared<const AssetCatalog>(first.publication->catalog);
        const auto record = catalog->records().at(id);
        require(record.dependency_edges.size() == 2 && record.dependencies.size() == 2,
                "Particle publication omitted mesh/material dependency projection");
        require(load(root, catalog, id) == document->source().compiled(),
                "Particle pool loaded different values");
        auto second = publish(service, document->locator(), id);
        require(second.published && second.cache_hit,
                "Unchanged particle import did not reuse admitted cache");
        catalog = std::make_shared<const AssetCatalog>(second.publication->catalog);
        const auto copied_id = AssetId::generate();
        const auto copied = nlohmann::json::parse(rewrite_authored_asset(
            record, changed.dump(), "Assets/Copy.particles.json", {{id, copied_id}}));
        require(copied.at("asset_id").get<AssetId>() == copied_id &&
                    copied.at("mesh") == changed.at("mesh") &&
                    copied.at("extensions") == changed.at("extensions"),
                "Particle duplicate changed external references/extensions");
        reject([&] {
            rewrite_authored_asset(record, changed.dump(), "Assets/Wrong.json", {{id, copied_id}});
        });
        const std::array drafts{AssetReferenceDocument{document->locator(), changed}};
        const auto references =
            collect_asset_references({{"components", nlohmann::json::array()}}, drafts);
        require(references.references.size() == 2 && references.uninspected.size() == 1,
                "Particle reference impact did not report mesh/material and opaque extension");
        const auto sources = scan_asset_sources(root);
        require(sources.complete &&
                    sources.files.at(document->locator()).source_kind == ParticleEffectAsset::type,
                "Content discovery did not recognize Particle Effect source");
        const auto package = root / "Export";
        const std::array roots{id};
        (void)package_runtime_content(root, package, roots, {"linux", "none"});
        const auto packaged =
            std::make_shared<const AssetCatalog>(open_runtime_content(package, {"linux", "none"}));
        require(load(package, packaged, id) == document->source().compiled() &&
                    !std::filesystem::exists(package / document->locator()),
                "Source-free particle package differs or shipped source");
        std::filesystem::remove(root / document->locator());
        require(load(root, catalog, id) == document->source().compiled(),
                "Particle resource tried to reread offline source");
        auto invalid = *catalog;
        auto altered = invalid.records().at(id);
        altered.metadata["forge.particle_effect"]["sha256"] = std::string(64, '0');
        invalid.replace(altered);
        reject([&] { load(root, std::make_shared<const AssetCatalog>(invalid), id); });
        altered = catalog->records().at(id);
        altered.dependency_edges.front().revision = std::string(64, '0');
        invalid.replace(altered);
        reject([&] { load(root, std::make_shared<const AssetCatalog>(invalid), id); });
        reject([&] {
            ParticleEffectSource::parse(std::vector<std::byte>(particle_effect_byte_limit + 1));
        });
        std::cout << "Particle source history, immutable publication, references and source-free "
                     "package passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
