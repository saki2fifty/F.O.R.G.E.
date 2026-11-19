#include <forge/assets.hpp>
#include <fstream>
#include <iostream>

using namespace forge;
namespace {
void check(bool value, const char* reason) {
    if (!value)
        throw std::runtime_error(reason);
}
void write(const std::filesystem::path& path, const std::string& bytes) {
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    stream.exceptions(std::ios::badbit | std::ios::failbit);
    stream << bytes;
    stream.close();
}
template <class Action> void rejects(Action action) {
    try {
        action();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error("Invalid index refresh was admitted");
}
} // namespace
int main(int argc, char** argv) {
    try {
        check(argc == 2, "Need a private test output root");
        const auto output = std::filesystem::absolute(argv[1]) / AssetId::generate().str();
        const auto root = output / "Project";
        std::filesystem::create_directories(root);
        AssetCatalog observed(root);
        check(observed.refresh_project() && !observed.refresh_project() &&
                  observed.records().empty(),
              "Absent index observation did not remain empty and reusable");
        write(root / "first.txt", "first");
        write(root / "second.txt", "second");
        AssetCatalog published(root);
        AssetRecord record{AssetId::generate(),
                           "refresh.test",
                           "first.txt",
                           1,
                           {},
                           nlohmann::json::object(),
                           {},
                           {},
                           {}};
        record.metadata = {{"revision", "a"}};
        published.add(record);
        const auto index = AssetCatalog::project_index(root);
        published.save(index);
        check(observed.refresh_project() && !observed.refresh_project() &&
                  observed.document() == published.document(),
              "Fresh or unchanged published records were not observed correctly");
        const auto stamp = std::filesystem::last_write_time(index);
        const auto old_bytes = published.document().dump(2);
        record.metadata["revision"] = "b";
        published.replace(record);
        const auto bytes = published.document().dump(2);
        check(bytes.size() == old_bytes.size(), "Changed index must preserve fixture byte count");
        write(index, bytes);
        std::filesystem::last_write_time(index, stamp);
        check(observed.refresh_project() && observed.document() == published.document(),
              "Equal-size/equal-time changed bytes reused stale records");
        auto malformed = bytes;
        malformed.front() = '!';
        write(index, malformed);
        std::filesystem::last_write_time(index, stamp);
        for (unsigned attempt = 0; attempt < 2; ++attempt) {
            rejects([&] { (void)observed.refresh_project(); });
            check(observed.document() == published.document(),
                  "Failed refresh replaced the prior validated value");
        }
        write(index, bytes);
        std::filesystem::last_write_time(index, stamp);
        check(!observed.refresh_project(),
              "Repair to the exact prior bytes lost reusable validation");
        const auto restores_published = [&] {
            check(observed.refresh_project() && observed.document() == published.document() &&
                      !observed.refresh_project(),
                  "Local mutation hid the authoritative published index during refresh");
        };
        observed.add({AssetId::generate(),
                      "refresh.test",
                      "second.txt",
                      1,
                      {},
                      nlohmann::json::object(),
                      {},
                      {},
                      {}});
        restores_published();
        auto local = record;
        local.metadata["revision"] = "unsaved";
        observed.replace(local);
        restores_published();
        observed.replace_all({});
        restores_published();
        observed.relocate(record.id, "second.txt");
        restores_published();
        observed.set_dependencies(record.id, {});
        restores_published();
        std::filesystem::remove(index);
        check(observed.refresh_project() && observed.records().empty() &&
                  !observed.refresh_project(),
              "Removed index retained stale records");
        published.save(index);
        check(observed.refresh_project() && observed.document() == published.document(),
              "A recreated index did not recover published records");
        const AssetRecord second{AssetId::generate(),
                                 "refresh.test",
                                 "second.txt",
                                 1,
                                 {},
                                 nlohmann::json::object(),
                                 {},
                                 {},
                                 {}};
        published.add(second);
        write(root / "dependency.txt", "dependency");
        published.set_source_dependencies(record.id, {{"dependency.txt", "refresh.input", {}}});
        published.save(index);
        check(observed.refresh_project(), "Additional source records were not admitted");
        const auto stable = observed.document();
        std::filesystem::remove(root / "second.txt");
        std::filesystem::create_hard_link(root / "first.txt", root / "second.txt");
        for (unsigned attempt = 0; attempt < 2; ++attempt) {
            rejects([&] { (void)observed.refresh_project(); });
            check(observed.document() == stable,
                  "A new filesystem alias changed the retained catalog");
        }
        std::filesystem::remove(root / "second.txt");
        write(root / "second.txt", "second");
        check(!observed.refresh_project(), "Repaired file identity lost reusable metadata");
        write(output / "outside.txt", "outside");
        for (const auto* name : {"second.txt", "dependency.txt"}) {
            std::filesystem::remove(root / name);
            std::error_code error;
            std::filesystem::create_symlink(output / "outside.txt", root / name, error);
            if (!error) {
                rejects([&] { (void)observed.refresh_project(); });
                check(observed.document() == stable,
                      "A source or source dependency escaped a reused catalog's scope");
                std::filesystem::remove(root / name);
            } else {
                std::cout << "Symbolic-link fixture unavailable: " << error.message() << '\n';
            }
            write(root / name, "repaired");
            check(!observed.refresh_project(), "Repaired path did not recover existing metadata");
        }
        write(root / "container.bin", "container");
        write(root / "another.bin", "another");
        std::error_code link_error;
        std::filesystem::create_symlink(root / "container.bin", root / "member.bin", link_error);
        if (!link_error) {
            AssetRecord container{AssetId::generate(),
                                  "refresh.container",
                                  "container.bin",
                                  1,
                                  {},
                                  nlohmann::json::object(),
                                  {},
                                  {},
                                  {}};
            AssetRecord member{AssetId::generate(),
                               "refresh.member",
                               "member.bin",
                               1,
                               {},
                               nlohmann::json::object(),
                               {},
                               {},
                               {}};
            member.subasset = AssetSubasset{container.id, "member", false};
            published.replace_all({container, member});
            published.save(index);
            check(observed.refresh_project(), "Shared canonical container source was not admitted");
            const auto container_value = observed.document();
            std::filesystem::remove(root / "member.bin");
            std::filesystem::create_symlink(root / "another.bin", root / "member.bin");
            rejects([&] { (void)observed.refresh_project(); });
            check(observed.document() == container_value,
                  "A retargeted subasset source changed its retained logical family");
            std::filesystem::remove(root / "member.bin");
            std::filesystem::create_symlink(root / "container.bin", root / "member.bin");
            check(!observed.refresh_project(), "Repaired container binding lost reusable metadata");
        }
        std::filesystem::remove_all(output);
        std::cout << "Catalog exact-byte reuse, equal-time edits, retry and mutation invalidation "
                     "passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
