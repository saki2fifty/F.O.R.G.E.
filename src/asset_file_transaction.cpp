#include "asset_file_transaction.hpp"
#include "asset_bytes.hpp"
#include "asset_storage.hpp"
#include "bounded_json.hpp"
#include <forge/assets.hpp>
#include <set>
namespace forge {
#ifdef FORGE_ASSET_FILE_TRANSACTION_TESTING
void asset_file_transaction_checkpoint(unsigned);
#endif
namespace {
using Json = nlohmann::json;
using namespace asset_storage;
constexpr std::size_t file_limit = 512ull * 1024 * 1024;
constexpr std::size_t journal_limit = 4 * 1024 * 1024;
constexpr std::uint64_t total_limit = 2ull * 1024 * 1024 * 1024;
std::string digest(std::string_view value) {
    return asset_detail::content_digest(std::as_bytes(std::span(value)));
}
Json fingerprint(const std::shared_ptr<const std::string>& value) {
    return value ? Json(digest(*value)) : Json();
}
Json current(const std::filesystem::path& path) {
    const auto value = read(path, file_limit);
    return value ? Json(digest(*value)) : Json();
}
void checkpoint(unsigned value) {
#ifdef FORGE_ASSET_FILE_TRANSACTION_TESTING
    asset_file_transaction_checkpoint(value);
#else
    (void)value;
#endif
}
void cancel(std::stop_token stop) {
    if (stop.stop_requested())
        throw std::runtime_error("Asset file operation cancelled");
}
std::filesystem::path directory(const ProjectPaths& paths, const std::string& id) {
    (void)AssetId::parse(id);
    return paths.resolve(std::filesystem::path(".forge/asset-file-operations") / id);
}
void source_path(const ProjectPaths& paths, const std::filesystem::path& path) {
    if (ProjectPaths::normalize(path) != path)
        throw std::runtime_error("Asset operation needs a normalized locator");
    auto text = path_utf8(path);
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) {
        return c >= 'A' && c <= 'Z' ? char(c + ('a' - 'A')) : char(c);
    });
    if (text.starts_with(".forge/") || text == ".forge" || text.starts_with(".git/") ||
        text == ".git" || text == "forge.project.json" || text.starts_with("forge.assets.json."))
        throw std::runtime_error("Asset operation cannot modify reserved project metadata");
    const auto absolute = paths.resolve(path);
    ordinary(absolute);
    if (!std::filesystem::is_directory(absolute.parent_path()))
        throw std::runtime_error("Asset destination folder does not exist");
}
void check_digest(const Json& value) {
    if (!value.is_null() && (!value.is_string() || !valid_content_digest(value.get<std::string>())))
        throw std::runtime_error("Invalid asset operation byte revision");
}
Json load_journal(const ProjectPaths& paths, std::string_view data) {
    auto result = asset_detail::parse_bounded_json(std::as_bytes(std::span(data)), journal_limit);
    if (result.at("format") != "forge.asset-file-operation" || result.at("version") != 1 ||
        !result.at("retain_backups").is_boolean() || !result.at("changes").is_array() ||
        result.at("changes").empty() || result.at("changes").size() > 8192)
        throw std::runtime_error("Unsupported asset operation recovery record");
    (void)directory(paths, result.at("transaction").get<std::string>());
    std::set<std::filesystem::path, ProjectLocatorLess> locators;
    const auto& changes = result.at("changes");
    for (std::size_t i = 0; i < changes.size(); ++i) {
        const auto& change = changes[i];
        const auto path = std::filesystem::u8path(change.at("source").get<std::string>());
        source_path(paths, path);
        if (!locators.insert(path).second ||
            paths.same_locator(path, "forge.assets.json") != (i + 1 == changes.size()) ||
            (i + 1 == changes.size() && path != std::filesystem::path("forge.assets.json")))
            throw std::runtime_error("Asset operation needs one final catalog commit point");
        check_digest(change.at("before"));
        check_digest(change.at("after"));
    }
    const auto& catalog = changes.back();
    if (catalog.at("after").is_null() || catalog.at("before") == catalog.at("after"))
        throw std::runtime_error("Asset operation catalog commit must change its revision");
    return result;
}
std::string blob(const std::filesystem::path& folder, const Json& key) {
    check_digest(key);
    if (key.is_null())
        throw std::runtime_error("Missing asset operation backup revision");
    const auto value = read(folder / key.get<std::string>(), file_limit);
    if (!value || digest(*value) != key.get<std::string>())
        throw std::runtime_error("Asset operation backup is missing or corrupt");
    return *value;
}
void validate_blobs(const ProjectPaths& paths, const Json& record) {
    const auto folder = directory(paths, record.at("transaction").get<std::string>());
    std::set<std::string> verified;
    std::uint64_t total = 0;
    for (const auto& change : record.at("changes"))
        for (const auto* name : {"before", "after"}) {
            const auto& key = change.at(name);
            if (!key.is_null() && verified.insert(key.get<std::string>()).second) {
                const auto value = blob(folder, key);
                total += value.size();
                if (total > total_limit)
                    throw std::runtime_error("Asset operation backup bytes exceed 2 GiB");
            }
        }
    for (const auto* name : {"before", "after"}) {
        const auto& key = record.at("changes").back().at(name);
        if (!key.is_null()) {
            const auto value = blob(folder, key);
            AssetCatalog candidate(paths.root());
            candidate.restore(asset_detail::parse_bounded_json(std::as_bytes(std::span(value)),
                                                               max_asset_index_bytes));
        }
    }
}
void install(const ProjectPaths& paths, const std::filesystem::path& folder, const Json& change,
             const char* version) {
    const auto path =
        paths.resolve(std::filesystem::u8path(change.at("source").get<std::string>()));
    const auto& key = change.at(version);
    if (key.is_null())
        erase_file(path);
    else
        replace(path, blob(folder, key));
}
// Remove only known, verified staging files. Unknown/external files are retained.
void cleanup(const ProjectPaths& paths, const Json& record) {
    const auto folder = directory(paths, record.at("transaction").get<std::string>());
    if (record.at("retain_backups").get<bool>())
        return;
    std::set<std::string> keys;
    for (const auto& change : record.at("changes"))
        for (const auto* name : {"before", "after"})
            if (!change.at(name).is_null())
                keys.insert(change.at(name).get<std::string>());
    for (const auto& key : keys)
        if (std::filesystem::exists(folder / key)) {
            (void)blob(folder, key);
            erase_file(folder / key);
        }
    const auto manifest = folder / "operation.json";
    if (const auto bytes = read(manifest, journal_limit)) {
        if (*bytes != record.dump())
            throw std::runtime_error("Asset operation staging manifest changed externally");
        erase_file(manifest);
    }
    if (std::filesystem::is_empty(folder)) {
        std::filesystem::remove(folder);
        sync_directory(folder.parent_path());
    }
}
} // namespace
std::filesystem::path AssetFileTransaction::journal() { return ".forge/asset-file-operation.json"; }
AssetFileTransaction::AssetFileTransaction(const ProjectLease& lease) : lease_(lease) { check(); }
void AssetFileTransaction::check() const {
    if (owner_ != std::this_thread::get_id())
        throw std::runtime_error("Asset file transaction requires its owning worker/thread");
    lease_.check();
}
bool AssetFileTransaction::recover() {
    check();
    const ProjectPaths paths(lease_.root());
    const auto data = read(paths.resolve(journal()), journal_limit);
    if (!data)
        return false;
    if (std::filesystem::exists(paths.resolve(".forge/asset-publication.json")))
        throw std::runtime_error("Conflicting asset file/publication recovery records; preserved");
    const auto record = load_journal(paths, *data);
    validate_blobs(paths, record);
    const auto& changes = record.at("changes");
    const auto folder = directory(paths, record.at("transaction").get<std::string>());
    // Preflight every path before recovery writes anything.
    for (const auto& change : changes) {
        const auto actual =
            current(paths.resolve(std::filesystem::u8path(change.at("source").get<std::string>())));
        if (actual != change.at("before") && actual != change.at("after"))
            throw std::runtime_error("Asset operation recovery conflicts with external edits: " +
                                     change.at("source").get<std::string>());
    }
    const bool committed =
        current(AssetCatalog::project_index(paths.root())) == changes.back().at("after");
    if (committed) {
        for (const auto& change : changes)
            if (current(paths.resolve(std::filesystem::u8path(
                    change.at("source").get<std::string>()))) != change.at("after"))
                throw std::runtime_error("Committed asset operation has inconsistent source bytes");
    } else {
        for (auto it = changes.rbegin(); it != changes.rend(); ++it)
            if (current(paths.resolve(std::filesystem::u8path(
                    it->at("source").get<std::string>()))) != it->at("before"))
                install(paths, folder, *it, "before");
    }
    // Remove the authority before disposable cleanup, which may be interrupted.
    erase_file(paths.resolve(journal()));
    cleanup(paths, record);
    return true;
}
AssetFileCommit AssetFileTransaction::commit(std::vector<AssetFileChange> changes,
                                             bool retain_backups, std::stop_token stop) {
    check();
    cancel(stop);
    const ProjectPaths paths(lease_.root());
    if (std::filesystem::exists(paths.resolve(journal())) ||
        std::filesystem::exists(paths.resolve(".forge/asset-publication.json")))
        throw std::runtime_error("Recover the interrupted asset operation/publication first");
    Json record = {{"format", "forge.asset-file-operation"},
                   {"version", 1},
                   {"transaction", AssetId::generate().str()},
                   {"retain_backups", retain_backups},
                   {"changes", Json::array()}};
    std::uint64_t total = 0;
    for (const auto& change : changes) {
        cancel(stop);
        for (const auto& bytes : {change.before, change.after})
            if (bytes) {
                total += bytes->size();
                if (bytes->size() > file_limit || total > total_limit)
                    throw std::runtime_error(
                        "Asset operation exceeds source/aggregate byte limits");
            }
        record["changes"].push_back({{"source", path_utf8(change.source)},
                                     {"before", fingerprint(change.before)},
                                     {"after", fingerprint(change.after)}});
    }
    const auto encoded = record.dump();
    record = load_journal(paths, encoded);
    for (const auto& change : record.at("changes"))
        if (current(paths.resolve(std::filesystem::u8path(
                change.at("source").get<std::string>()))) != change.at("before"))
            throw std::runtime_error("Asset source/catalog changed since operation review: " +
                                     change.at("source").get<std::string>());
    const auto id = record.at("transaction").get<std::string>();
    const auto folder = directory(paths, id);
    ordinary(folder);
    std::filesystem::create_directories(folder);
    sync_directory(folder.parent_path());
    sync_directory(paths.resolve(".forge"));
    replace(folder / "operation.json", encoded);
    bool started = false;
    try {
        std::set<std::string> stored;
        for (const auto& change : changes)
            for (const auto& bytes : {change.before, change.after})
                if (bytes) {
                    cancel(stop);
                    const auto key = digest(*bytes);
                    if (stored.insert(key).second)
                        replace(folder / key, *bytes);
                }
        validate_blobs(paths, record);
        cancel(stop);
        replace(paths.resolve(journal()), encoded);
        started = true;
        checkpoint(1);
        unsigned step = 1;
        for (const auto& change : record.at("changes")) {
            cancel(stop);
            if (current(paths.resolve(std::filesystem::u8path(
                    change.at("source").get<std::string>()))) != change.at("before"))
                throw std::runtime_error("Asset source changed during file operation");
            install(paths, folder, change, "after");
            checkpoint(++step);
        }
    } catch (...) {
        if (started) {
            const bool committed = current(AssetCatalog::project_index(paths.root())) ==
                                   record.at("changes").back().at("after");
            recover();
            if (committed)
                return {id, retain_backups ? paths.relative(folder) : std::filesystem::path{},
                        "File operation committed; interrupted cleanup was recovered."};
        } else {
            auto disposable = record;
            disposable["retain_backups"] = false;
            // Manifest retains the original value; cleanup requires exact bytes.
            replace(folder / "operation.json", disposable.dump());
            cleanup(paths, disposable);
        }
        throw;
    }
    AssetFileCommit result{
        id, retain_backups ? paths.relative(folder) : std::filesystem::path{}, {}};
    try {
        erase_file(paths.resolve(journal()));
        cleanup(paths, record);
    } catch (const std::exception& e) {
        result.cleanup_diagnostic = e.what();
    }
    return result;
}
} // namespace forge
