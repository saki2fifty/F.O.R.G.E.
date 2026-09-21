#include "asset_file_transaction.hpp"
#include "asset_storage.hpp"
#include <cstdlib>
#include <forge/asset_publication.hpp>
#include <fstream>
#include <future>
#include <iostream>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
using namespace forge;
namespace {
unsigned crash_stage = 0;
unsigned throw_stage = 0;
std::stop_source* cancel_source = nullptr;
void check(bool value, const char* why) {
    if (!value)
        throw std::runtime_error(why);
}
template <class F> void rejects(F&& action) {
    try {
        action();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error("Invalid asset file operation accepted");
}
auto bytes(std::string text) { return std::make_shared<const std::string>(std::move(text)); }
std::filesystem::path project(std::filesystem::path path) {
    std::filesystem::create_directories(path);
    return std::filesystem::canonical(path);
}
struct Fixture {
    std::filesystem::path root;
    ProjectLease lease;
    AssetFileTransaction operations;
    std::shared_ptr<const std::string> before, after;
    explicit Fixture(std::filesystem::path path, bool initialize)
        : root(project(path)), lease(root), operations(lease) {
        std::filesystem::create_directories(root / "Assets");
        AssetCatalog catalog(root);
        if (initialize) {
            catalog.add({AssetId::generate(), "fixture", "Assets/source.fixture", 1, {}});
            catalog.save(AssetCatalog::project_index(root));
            asset_storage::replace(root / "Assets/source.fixture", "source bytes");
        }
        before = bytes(*asset_storage::read(AssetCatalog::project_index(root)));
        catalog.restore(nlohmann::json::parse(*before));
        auto record = catalog.records().begin()->second;
        record.source = "Assets/moved.fixture";
        catalog.replace(record);
        after = bytes(catalog.document().dump(2));
    }
    std::vector<AssetFileChange> move() const {
        auto source = bytes("source bytes");
        return {{"Assets/source.fixture", source, {}},
                {"Assets/moved.fixture", {}, source},
                {"forge.assets.json", before, after}};
    }
    void assert_before() const {
        check(asset_storage::read(root / "Assets/source.fixture") == "source bytes" &&
                  !std::filesystem::exists(root / "Assets/moved.fixture") &&
                  asset_storage::read(AssetCatalog::project_index(root)) == *before,
              "Precommit failure changed source/catalog");
    }
};
} // namespace
namespace forge {
void asset_file_transaction_checkpoint(unsigned stage) {
    if (stage == crash_stage)
        std::_Exit(90 + int(stage));
    if (stage == throw_stage)
        throw std::runtime_error("Injected asset file operation interruption");
    if (stage == 2 && cancel_source)
        cancel_source->request_stop();
}
} // namespace forge
int main(int argc, char** argv) {
    try {
        if (argc < 2)
            throw std::runtime_error("Expected project path");
        const bool recovery = argc > 2 && std::string(argv[2]) == "--recover";
        Fixture f(argv[1], !recovery);
        if (recovery) {
            check(f.operations.recover(), "Expected interrupted transaction");
            const unsigned stage = unsigned(std::stoul(argv[3]));
            const auto catalog = AssetCatalog::open_project(f.root);
            const auto expected = stage == 4 ? "Assets/moved.fixture" : "Assets/source.fixture";
            check(catalog.records().begin()->second.source == expected &&
                      asset_storage::read(f.root / expected) == "source bytes",
                  "Process recovery selected incorrect source/catalog");
            check(!f.operations.recover(), "Recovery was not idempotent");
            return 0;
        }
        if (argc > 2) {
            crash_stage = unsigned(std::stoul(argv[3]));
            f.operations.commit(f.move(), false);
            throw std::runtime_error("Expected process interruption");
        }
        {
            // Valid destination near the traditional Windows path bound. The old
            // destination-name + UUID temporary exceeded it (and long names could
            // exceed POSIX's per-component bound).
            const auto parent_chars = f.root.native().size() + 1;
            const auto name_chars = parent_chars < 176 ? 240 - parent_chars : std::size_t{64};
            const auto destination = f.root / std::string(name_chars, 'n');
            asset_storage::replace(destination, "first");
            asset_storage::replace(destination, "replacement");
            check(asset_storage::read(destination) == "replacement",
                  "Long valid destination could not be atomically replaced");
            std::filesystem::remove(destination);
        }
        auto invalid = f.move();
        invalid[0].before = bytes("wrong revision");
        rejects([&] { f.operations.commit(invalid, false); });
        f.assert_before();
        invalid = f.move();
        invalid[1].source = ".forge/forbidden";
        rejects([&] { f.operations.commit(invalid, false); });
        invalid = f.move();
        invalid.back().after = bytes("invalid catalog");
        rejects([&] { f.operations.commit(invalid, false); });
        f.assert_before();
        for (unsigned stage : {1u, 2u, 3u}) {
            throw_stage = stage;
            rejects([&] { f.operations.commit(f.move(), false); });
            throw_stage = 0;
            f.assert_before();
            check(!f.operations.recover(), "Exception left an active recovery journal");
        }
        std::stop_source cancel;
        cancel_source = &cancel;
        rejects([&] { f.operations.commit(f.move(), false, cancel.get_token()); });
        cancel_source = nullptr;
        f.assert_before();
        check(std::async(std::launch::async,
                         [&] {
                             try {
                                 f.operations.recover();
                             } catch (const std::exception&) {
                                 return true;
                             }
                             return false;
                         })
                  .get(),
              "Off-owner file operation accepted");
#ifdef _WIN32
        const auto locked = CreateFileW((f.root / "Assets/source.fixture").c_str(), GENERIC_READ,
                                        FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
        check(locked != INVALID_HANDLE_VALUE, "Cannot hold Windows source deletion lock");
        bool blocked = false;
        try {
            (void)f.operations.commit(f.move(), false);
        } catch (const std::exception&) {
            blocked = true;
        }
        CloseHandle(locked);
        check(blocked, "Windows source deletion lock did not reject the operation");
        f.assert_before();
        check(!f.operations.recover(), "Sharing failure left incomplete recovery");
#endif
        throw_stage = 4;
        const auto committed = f.operations.commit(f.move(), true);
        throw_stage = 0;
        check(!committed.cleanup_diagnostic.empty() && !committed.retained_files.empty() &&
                  std::filesystem::is_regular_file(f.root / committed.retained_files /
                                                   "operation.json"),
              "Postcommit interruption lost success/retained backup receipt");
        check(!std::filesystem::exists(f.root / "Assets/source.fixture") &&
                  asset_storage::read(f.root / "Assets/moved.fixture") == "source bytes" &&
                  asset_storage::read(AssetCatalog::project_index(f.root)) == *f.after,
              "Postcommit interruption rolled back successful operation");
        check(!f.operations.recover(), "Completed operation retained recovery authority");
        std::cout << "Asset file revision/rollback/commit/backup tests passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
