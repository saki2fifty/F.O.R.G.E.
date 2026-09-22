#include "asset_bytes.hpp"
#include "cache_maintenance.hpp"
#include <forge/derived_cache.hpp>
#include <fstream>
#include <iostream>
using namespace forge;
namespace {
void check(bool ok, const char* why) {
    if (!ok)
        throw std::runtime_error(why);
}
template <class F> void rejects(F run) {
    try {
        run();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error("Invalid cache operation accepted");
}
void write(const std::filesystem::path& path, std::string_view text) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary);
    output << text;
    check(bool(output.flush()), "Cache fixture write failed");
}
} // namespace
int main(int argc, char** argv) {
    try {
        check(argc == 2, "Need scratch directory");
        const auto root = std::filesystem::absolute(argv[1]) / AssetId::generate().str();
        std::filesystem::create_directories(root);
        struct Cleanup {
            std::filesystem::path root;
            ~Cleanup() {
                std::error_code ignored;
                std::filesystem::remove_all(root, ignored);
            }
        } scratch_cleanup{root};
        ProjectLease lease(root);
        DerivedDataCache cache(root);
        AssetBuildInput input;
        input.source_digest = std::string(64, 'a');
        input.importer = "fixture.cache";
        input.importer_revision = std::string(64, 'b');
        input.output_format = "fixture.bytes";
        input.platform = "portable";
        input.backend = "none";
        input.profile = "cpu";
        const std::vector<ArtifactFile> files{{"data.bin", {std::byte{42}}}};
        const auto validate = [&](const CachedArtifact& value) {
            check(value.files.size() == 1 && value.files[0].bytes == files[0].bytes,
                  "Fixture cache bytes changed");
        };
        const auto selected = cache.publish(input, files, validate);
        input.source_digest = std::string(64, 'c');
        const auto old = cache.publish(input, files, validate);
        const auto id = AssetId::generate();
        write(root / "Assets/Source.bin", "authored data");
        AssetRecord record{id, "fixture", "Assets/Source.bin", 1, {}};
        record.metadata["forge.import"] = {{"key", selected.key}};
        AssetCatalog catalog(root);
        catalog.add(record);
        catalog.save(AssetCatalog::project_index(root));
        const auto before = asset_detail::read_bytes(AssetCatalog::project_index(root), 65536);
        const auto stats = maintain_asset_cache(lease, CacheMaintenance::Statistics);
        check(stats.at("statistics").at("entries") == 2, "Cache statistics lost entries");
        const auto verified = maintain_asset_cache(lease, CacheMaintenance::Verify);
        check(verified.at("ok") == true && verified.at("entries").size() == 2 &&
                  verified.at("entries")[0].at("format_validated") == false,
              "Storage audit pretended to perform runtime format admission");
        maintain_asset_cache(lease, CacheMaintenance::PruneUnused, {}, 0);
        check(cache.keys() == std::set{selected.key}, "Prune removed the selected artifact");
        rejects([&] { cache.erase({"../outside"}); });
        rejects([&] {
            maintain_asset_cache(lease, CacheMaintenance::ClearAsset, AssetId::generate());
        });
        const auto cleared = maintain_asset_cache(lease, CacheMaintenance::ClearAsset, id);
        check(cleared.at("affected_assets") == nlohmann::json::array({id}) && cache.keys().empty(),
              "Selected cache removal lost affected identity or left bytes");
        check(selected.files[0].bytes == files[0].bytes,
              "Cache deletion invalidated an existing owned artifact");
        const auto directory = root / ".forge/cache/derived";
        cache.publish(input, files, validate);
        const auto nested_key = std::string(64, 'f');
        write(directory / nested_key / "unexpected/nested.txt", "preserve");
        rejects([&] { cache.erase({old.key, nested_key}); });
        rejects([&] { cache.prune(0, {}); });
        check(std::filesystem::exists(directory / old.key / "data.bin"),
              "Cache maintenance partially deleted before rejecting an unfamiliar candidate");
        check(std::filesystem::exists(directory / nested_key / "unexpected/nested.txt"),
              "Cache maintenance deleted unfamiliar nested contents");
        std::filesystem::remove_all(directory / nested_key);
        cache.erase({old.key});
        auto orphan = directory / ("pending-" + AssetId::generate().str());
        write(orphan / "data.bin", "interrupted publication");
        auto unfamiliar = directory / ("pending-" + AssetId::generate().str());
        write(unfamiliar / "unexpected/nested.txt", "preserve");
        auto lookalike = directory / "pending-user-notes";
        write(lookalike / "notes.txt", "preserve");
        const auto cleanup = maintain_asset_cache(lease, CacheMaintenance::Cleanup);
        check(cleanup.at("ok") == false && !std::filesystem::exists(orphan) &&
                  std::filesystem::exists(unfamiliar / "unexpected/nested.txt") &&
                  std::filesystem::exists(lookalike / "notes.txt"),
              "Orphan cleanup deleted unfamiliar contents or retained owned flat staging");
        std::filesystem::remove_all(unfamiliar);
        std::filesystem::remove_all(lookalike);
        cache.publish(input, files, validate);
        write(directory / old.key / "data.bin", "corrupt");
        const auto corrupt = maintain_asset_cache(lease, CacheMaintenance::Verify);
        check(corrupt.at("ok") == false && std::filesystem::exists(directory / old.key),
              "Read-only integrity audit silently destroyed corrupt/unknown-provider evidence");
        // Normal format admission still rejects and quarantines the damaged candidate.
        check(!cache.find(input, validate) && cache.statistics().quarantined == 1,
              "Corrupt format admission failed to quarantine");
        auto all = maintain_asset_cache(lease, CacheMaintenance::ClearAll);
        check(all.at("ok") == true && cache.statistics().entries == 0 &&
                  cache.statistics().quarantined == 0,
              "Clear all left owned artifact/quarantine contents");
        check(before == asset_detail::read_bytes(AssetCatalog::project_index(root), 65536) &&
                  asset_detail::read_bytes(root / "Assets/Source.bin", 128).size() == 13,
              "Cache maintenance changed authored catalog or source");
        std::stop_source stop;
        stop.request_stop();
        rejects([&] {
            maintain_asset_cache(lease, CacheMaintenance::Verify, {}, 0, stop.get_token());
        });
        cache.publish(input, files, validate);
        check(cache.find(input, validate).has_value(), "Cleared cache could not rebuild");
        // Close the lease before deleting its Windows lock file.
        std::cout << "Cache maintenance identities, orphan preservation, format boundary and "
                     "rebuild passed: "
                  << root << '\n';
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
