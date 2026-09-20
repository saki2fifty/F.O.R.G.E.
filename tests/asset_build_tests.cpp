#include "asset_bytes.hpp"
#include <array>
#include <forge/asset_build.hpp>
#include <forge/assets.hpp>
#include <forge/derived_cache.hpp>
#include <forge/scene.hpp>
#include <future>
#include <iostream>
#include <limits>

using namespace forge;
namespace {
void require(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}
template <class F> void rejects(F fn) {
    bool rejected = false;
    try {
        fn();
    } catch (const std::exception&) {
        rejected = true;
    }
    require(rejected, "Invalid operation accepted");
}
std::vector<std::byte> bytes(std::string_view text) {
    auto source = std::as_bytes(std::span(text));
    return {source.begin(), source.end()};
}
AssetBuildInput input() {
    return {asset_detail::content_digest(bytes("source")),
            "forge.fixture",
            "source-1",
            1,
            {{"quality", 3}},
            {},
            {},
            "fixture",
            1,
            "portable",
            "none",
            "test"};
}
void admission(const CachedArtifact& artifact) {
    require(artifact.files.size() == 1 && artifact.files[0].name == "data.bin",
            "Unexpected fixture outputs");
    require(artifact.files[0].bytes == bytes("validated"), "Invalid fixture data");
}
} // namespace
int main(int argc, char** argv) {
    try {
        if (argc == 3 && std::string(argv[1]) == "--roundtrip") {
            const auto index = std::filesystem::absolute(argv[2]);
            AssetCatalog catalog(index.parent_path());
            catalog.load(index);
            const auto before = catalog.dependency_graph().document();
            auto destination = index;
            destination += ".roundtrip";
            require(!std::filesystem::exists(destination), "Scale roundtrip output already exists");
            catalog.save(destination);
            AssetCatalog reopened(index.parent_path());
            reopened.load(destination);
            require(reopened.records().size() == catalog.records().size() &&
                        reopened.dependency_graph().document() == before,
                    "Scale roundtrip changed records/dependency graph");
            std::cout << "Saved/reopened " << catalog.records().size() << " assets in "
                      << std::filesystem::file_size(destination) << " bytes\n";
            return 0;
        }
        require(argc == 2, "Expected scratch root");
        const auto root = std::filesystem::absolute(argv[1]) / AssetId::generate().str();
        std::filesystem::create_directories(root);
        struct Cleanup {
            std::filesystem::path path;
            ~Cleanup() {
                std::error_code ec;
                std::filesystem::remove_all(path, ec);
            }
        } cleanup{root};
        const auto a = AssetId::generate(), b = AssetId::generate(), c = AssetId::generate();
        const auto digest = asset_detail::content_digest(bytes("revision"));
        AssetCatalog catalog(root);
        catalog.add({a, "material", "a.material.json", 1, {}});
        catalog.add({b, "texture", "b.png", 1, {}});
        catalog.set_dependencies(
            a, {{b, "texture", AssetDependencyKind::Build, "base_color", digest}});
        rejects([&] {
            catalog.set_dependencies(a, {{b, "mesh", AssetDependencyKind::Build, "wrong", digest}});
        });
        require(catalog.dependency_graph().referrers(b) == std::vector<AssetId>{a},
                "Catalog reverse index");
        catalog.set_source_dependencies(b, {{"includes/../includes/image.bin", "buffer", digest}});
        require(catalog.dependency_graph().source_referrers("b.png") == std::vector<AssetId>{b},
                "Primary source is not indexed");
        require(catalog.dependency_graph().source_referrers("includes/image.bin") ==
                    std::vector<AssetId>{b},
                "Raw source dependency is not indexed");
        const auto source_affected =
            catalog.dependency_graph().invalidated_by_source("includes/image.bin");
        require(std::set<AssetId>(source_affected.begin(), source_affected.end()) ==
                    std::set<AssetId>{a, b},
                "Raw source failed to invalidate transitive logical dependents");
        rejects([&] { catalog.set_source_dependencies(b, {{"../escape", "buffer", digest}}); });
        rejects([&] { catalog.set_source_dependencies(b, {{"b.png", "forge.primary", digest}}); });
        require(catalog.dependency_graph().source_referrers("includes/image.bin") ==
                    std::vector<AssetId>{b},
                "Rejected source update changed graph");
        const auto index = root / "forge.assets.json";
        catalog.save(index);
        auto loaded = AssetCatalog::open_project(root);
        require(loaded.dependency_graph().document() == catalog.dependency_graph().document(),
                "Catalog typed edge roundtrip");
        const auto bulk_before = loaded.dependency_graph().document();
        auto invalid_record = catalog.records().at(b);
        invalid_record.type = "wrong-type";
        rejects([&] { loaded.replace_all({catalog.records().at(a), invalid_record}); });
        require(loaded.dependency_graph().document() == bulk_before && loaded.records().size() == 2,
                "Bulk type mismatch changed catalog");
        invalid_record = catalog.records().at(b);
        invalid_record.source = catalog.records().at(a).source;
        rejects([&] { loaded.replace_all({catalog.records().at(a), invalid_record}); });
        require(loaded.dependency_graph().document() == bulk_before,
                "Bulk duplicate locator changed graph");
        atomic_write(root / "physical-a", "same file");
        std::filesystem::create_hard_link(root / "physical-a", root / "physical-b");
        rejects([&] {
            loaded.replace_all(
                {{a, "texture", "physical-a", 1, {}}, {b, "texture", "physical-b", 1, {}}});
        });
        require(loaded.dependency_graph().document() == bulk_before,
                "Bulk hardlink alias changed graph");
        rejects([&] {
            auto cycle_a = catalog.records().at(a), cycle_b = catalog.records().at(b);
            cycle_b.dependencies = {a};
            cycle_b.dependency_edges = {
                {a, cycle_a.type, AssetDependencyKind::Build, "cycle", digest}};
            loaded.replace_all({cycle_a, cycle_b});
        });
        require(loaded.dependency_graph().document() == bulk_before, "Bulk cycle changed graph");
        rejects([&] {
            auto invalid = catalog.records().at(a);
            invalid.metadata["invalid"] = std::numeric_limits<double>::quiet_NaN();
            loaded.replace_all({invalid});
        });
        require(loaded.dependency_graph().document() == bulk_before,
                "Non-finite metadata damaged previous catalog");
        rejects([&] {
            auto invalid = catalog.records().at(a);
            invalid.dependency_edges[0].expected_type = "legacy-untyped";
            loaded.replace_all({invalid, catalog.records().at(b)});
        });
        const auto original_index = asset_detail::read_bytes(index, 4 * 1024 * 1024);
        auto v1 = Json::parse(
            reinterpret_cast<const char*>(original_index.data()),
            reinterpret_cast<const char*>(original_index.data() + original_index.size()));
        v1["version"] = 1;
        for (auto& item : v1["assets"])
            item.erase("dependency_edges");
        const auto original_v1 = v1.dump(2);
        atomic_write(index, original_v1);
        loaded = AssetCatalog::open_project(root);
        require(asset_detail::read_bytes(index, 4 * 1024 * 1024) == bytes(original_v1),
                "Read migrated on disk");
        for (const auto suffix : {".v1.backup.pending", ".pending"}) {
            const std::filesystem::path blocker = index.string() + suffix;
            std::filesystem::create_directory(blocker);
            atomic_write(blocker / "keep", "occupied");
            rejects([&] { loaded.save(index); });
            require(asset_detail::read_bytes(index, 4 * 1024 * 1024) == bytes(original_v1),
                    "Failed catalog migration replaced original");
            require(asset_detail::read_bytes(blocker / "keep", 100) == bytes("occupied"),
                    "Failed catalog migration removed unrelated pending contents");
            std::filesystem::remove(blocker / "keep");
            std::filesystem::remove(blocker);
        }
        loaded.save(index);
        require(asset_detail::read_bytes(index.string() + ".v1.backup", 4 * 1024 * 1024) ==
                    bytes(original_v1),
                "Index migration lost original backup");
        require(loaded.records().at(a).dependencies == std::vector<AssetId>{b},
                "Migration lost legacy reference");
        AssetDependencyGraph graph;
        graph.replace(a, {{b, "texture", AssetDependencyKind::Build, "base_color", digest}});
        graph.replace(b, {{c, "source", AssetDependencyKind::Source, "input", digest}});
        graph.replace_sources(c, {{"shaders/lighting.hlsli", "include", digest}});
        graph.replace_sources(a, {{"shaders/unrelated.hlsli", "include", digest}});
        require(graph.invalidated_by_source("shaders/lighting.hlsli").size() == 3,
                "Source include did not invalidate the whole affected chain");
        require(graph.invalidated_by_source("shaders/unrelated.hlsli") == std::vector<AssetId>{a},
                "Source edit invalidated unrelated assets");
        const auto with_sources = graph.document();
        rejects([&] {
            graph.replace_sources(c, {{"same", "role", digest}, {"a/../same", "role", digest}});
        });
        require(graph.document() == with_sources, "Duplicate source edge changed graph");
        graph.replace_sources(c, {{"shaders/new.hlsli", "include", digest}});
        require(graph.source_referrers("shaders/lighting.hlsli").empty() &&
                    graph.source_referrers("shaders/new.hlsli") == std::vector<AssetId>{c},
                "Source reverse edges retained stale includes");
#ifdef _WIN32
        require(graph.source_referrers("SHADERS/NEW.HLSLI") == std::vector<AssetId>{c},
                "Windows source dependency matching is case sensitive");
#endif
        const auto before = graph.document();
        require(graph.referrers(b) == std::vector<AssetId>{a}, "Reverse graph missing");
        auto affected = graph.invalidated_by(c);
        require(affected.size() == 2 &&
                    std::find(affected.begin(), affected.end(), a) != affected.end(),
                "Transitive invalidation missing");
        const AssetId roots[]{a};
        rejects([&] { (void)graph.build_order(std::vector<AssetId>(100001, a)); });
        require(graph.build_order(roots) == std::vector<AssetId>{c, b, a}, "Wrong build order");
        rejects(
            [&] { graph.replace(c, {{a, "model", AssetDependencyKind::Build, "loop", digest}}); });
        require(graph.document() == before && graph.referrers(a).empty(), "Cycle changed graph");
        graph.replace(c, {{a, "model", AssetDependencyKind::Runtime, "link", digest}});
        require(graph.build_order(roots).size() == 3 && graph.invalidated_by(a).size() == 2,
                "Runtime cycle loops or prevents build");
        AssetDependencyGraph restored;
        restored.restore(graph.document());
        require(restored.document() == graph.document(), "Graph round trip");
        auto malformed = graph.document();
        malformed["records"].push_back(malformed["records"][0]);
        rejects([&] { restored.restore(malformed); });
        require(restored.document() == graph.document(), "Invalid restore destroyed graph");
        rejects([&] {
            graph.replace(a, {{b, "texture", AssetDependencyKind::Build, "same", digest},
                              {b, "different", AssetDependencyKind::Build, "same", digest}});
        });
        auto settings = input();
        const auto original = settings.key();
        const std::vector<std::function<void(AssetBuildInput&)>> mutations{
            [&](auto& value) { value.source_digest = digest; },
            [](auto& value) { value.importer += ".other"; },
            [](auto& value) { value.importer_revision += ".other"; },
            [](auto& value) { ++value.settings_version; },
            [](auto& value) { value.settings["quality"] = 4; },
            [&](auto& value) { value.source_dependencies["include"] = digest; },
            [&](auto& value) {
                value.dependencies.push_back(
                    {b, "texture", AssetDependencyKind::Build, "input", digest});
            },
            [](auto& value) { value.output_format += ".other"; },
            [](auto& value) { ++value.output_version; },
            [](auto& value) { value.platform += ".other"; },
            [](auto& value) { value.backend += ".other"; },
            [](auto& value) { value.profile += ".other"; }};
        for (const auto& mutate : mutations) {
            auto altered = settings;
            mutate(altered);
            require(altered.key() != original, "Build input absent from key");
        }
        settings.dependencies = {{b, "texture", AssetDependencyKind::Build, "b", digest},
                                 {a, "texture", AssetDependencyKind::Build, "a", digest}};
        auto reordered = settings;
        std::reverse(reordered.dependencies.begin(), reordered.dependencies.end());
        require(settings.key() == reordered.key(), "Dependency iteration changes cache key");
        rejects([&] {
            auto bad = input();
            bad.settings["value"] = std::numeric_limits<double>::infinity();
            (void)bad.key();
        });
        rejects([&] {
            auto bad = input();
            bad.source_digest = "../outside";
            (void)bad.key();
        });
        DerivedDataCache cache(root, {1024, 2048, 4});
        const auto build = input();
        require(!cache.find(build, admission), "Missing cache reported hit");
        auto artifact = cache.publish(build, {{"data.bin", bytes("validated")}}, admission);
        require(cache.find(build, admission)->manifest == artifact.manifest, "Cache hit differs");
        const auto cache_root = root / ".forge/cache/derived";
        rejects([&] { cache.publish(build, {{"../outside", bytes("bad")}}, admission); });
        rejects([&] { cache.publish(build, {{"data.bin", bytes("invalid")}}, admission); });
        require(cache.find(build, admission).has_value(), "Failed candidate damaged good output");
        const auto permissive = [](const CachedArtifact&) {};
        rejects([&] { cache.publish(build, {{"data.bin", bytes("different")}}, permissive); });
        require(cache.find(build, admission).has_value(),
                "Nondeterministic candidate replaced good output");
        rejects([&] { (void)cache.find(build, {}); });
        atomic_write(cache_root / build.key() / "data.bin", "corrupt");
        require(!cache.find(build, admission), "Corrupt bytes reached decoder");
        require(cache.statistics().quarantined == 1, "Corrupt output not quarantined");
        cache.publish(build, {{"data.bin", bytes("validated")}}, admission);
        auto bad_manifest = artifact.manifest;
        bad_manifest["files"][0]["bytes"] = -1;
        atomic_write(cache_root / build.key() / "manifest.json", bad_manifest.dump());
        require(!cache.find(build, admission), "Negative output size accepted");
        cache.publish(build, {{"data.bin", bytes("validated")}}, admission);
        atomic_write(cache_root / build.key() / "unlisted.bin", "bad");
        require(!cache.find(build, admission), "Unmanifested output accepted");
        cache.publish(build, {{"data.bin", bytes("validated")}}, admission);
#ifndef _WIN32
        std::filesystem::remove_all(cache_root / build.key());
        const auto external = root / "outside-cache";
        std::filesystem::create_directory(external);
        atomic_write(external / "sentinel", "unchanged");
        std::filesystem::create_directory_symlink(external, cache_root / build.key());
        require(!cache.find(build, admission), "Symlink output accepted");
        require(std::distance(std::filesystem::directory_iterator(external),
                              std::filesystem::directory_iterator{}) == 1,
                "Quarantine wrote through redirected output");
        cache.publish(build, {{"data.bin", bytes("validated")}}, admission);
#endif
        std::vector<std::future<std::string>> writers;
        for (unsigned i = 0; i < 8; ++i)
            writers.push_back(std::async(std::launch::async, [&] {
                DerivedDataCache concurrent(root, {1024, 2048, 4});
                return concurrent.publish(build, {{"data.bin", bytes("validated")}}, admission).key;
            }));
        for (auto& writer : writers)
            require(writer.get() == build.key(), "Concurrent publication identity");
        require(cache.verify(admission).size() == 1, "Cache verification omitted entry");
        require(cache.prune(0, {build.key()}) == 0 && cache.find(build, admission).has_value(),
                "Prune evicted selected artifact");
        require(cache.prune(0, {}) > 0 && !cache.find(build, admission), "Cache clear ineffective");
        admission(artifact); // In-use owned bytes survive disk eviction.
        const auto retained_graph = catalog.dependency_graph().document();
        const auto nested = std::string(70, '[') + "0" + std::string(70, ']');
        atomic_write(index, nested);
        rejects([&] { catalog.load(index); });
        rejects([&] { catalog.save(index); });
        require(asset_detail::read_bytes(index, 1024) == bytes(nested) &&
                    catalog.dependency_graph().document() == retained_graph,
                "Rejected nested index changed disk or live catalog");
        const auto oversized = root / "too-large.assets.json";
        atomic_write(oversized, "");
        std::filesystem::resize_file(oversized, max_asset_index_bytes + 1);
        rejects([&] { catalog.load(oversized); });
        require(catalog.dependency_graph().document() == retained_graph,
                "Oversized index changed live catalog");
        std::cout << "Asset graph, deterministic keys, cache integrity, concurrency and eviction "
                     "passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
