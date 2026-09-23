#include "asset_bytes.hpp"
#include "asset_storage.hpp"
#include "bounded_json.hpp"
#include <algorithm>
#include <cmath>
#include <forge/game_settings.hpp>
#include <forge/game_storage.hpp>
namespace forge {
namespace {
using Json = nlohmann::json;
constexpr std::size_t save_limit = 8 * 1024 * 1024, settings_limit = 1024 * 1024;
std::filesystem::path prepare(const std::filesystem::path& base, const std::string& application) {
    if (!base.is_absolute() || !valid_application_id(application))
        throw std::runtime_error(
            "game.storage: Absolute user-data base and application ID required");
    std::filesystem::create_directories(base);
    const auto root = std::filesystem::canonical(base) / ("game-" + application);
    asset_storage::ordinary(root);
    std::filesystem::create_directory(root);
    return root;
}
void admitted_json(const Json& value, unsigned depth, std::size_t& nodes) {
    if (depth > 64 || ++nodes > 262144 || value.is_binary() || value.is_discarded() ||
        (value.is_number_float() && !std::isfinite(value.get<double>())))
        throw std::runtime_error("game.storage: Unsupported or excessive persistent JSON state");
    if (value.is_structured())
        for (const auto& child : value)
            admitted_json(child, depth + 1, nodes);
}
std::string encoded(const Json& value, std::size_t limit) {
    std::size_t nodes = 0;
    admitted_json(value, 0, nodes);
    auto bytes = value.dump();
    if (bytes.size() > limit)
        throw std::runtime_error("game.storage: Persistent document exceeds its byte limit");
    return bytes;
}
std::string digest(const Json& payload) {
    const auto bytes = encoded(payload, save_limit);
    return asset_detail::content_digest(
        {reinterpret_cast<const std::byte*>(bytes.data()), bytes.size()});
}
Json envelope(Json payload) {
    return {{"format", "forge.game-user-data"},
            {"version", 1},
            {"sha256", digest(payload)},
            {"payload", std::move(payload)}};
}
Json read(const std::filesystem::path& path, const std::string& application, std::string_view kind,
          std::size_t limit) {
    auto bytes = asset_storage::read(path, limit);
    if (!bytes)
        throw std::runtime_error("game.storage: User document does not exist");
    auto document =
        asset_detail::parse_bounded_json(std::as_bytes(std::span(*bytes)), limit, 1048576, 64);
    if (!document.is_object() || document.at("format") != "forge.game-user-data" ||
        !document.at("version").is_number_integer() || document.at("version") != 1)
        throw std::runtime_error("game.storage: Unsupported user document envelope");
    auto payload = document.at("payload");
    if (document.at("sha256") != digest(payload) || payload.at("application_id") != application ||
        payload.at("kind").get<std::string>() != kind)
        throw std::runtime_error("game.storage: Corrupt or foreign user document");
    return payload;
}
void schema(const GameSaveSchema& value) {
    if (value.version == 0 || value.version > 1000000 || !value.validate)
        throw std::runtime_error("game.save: Explicit schema version and validator required");
}
void validate(const GameSave& value, const GameSaveSchema& contract) {
    if (!value.scene || !value.data.is_object())
        throw std::runtime_error("game.save: Scene AssetId and persistent data object required");
    (void)encoded(value.data, save_limit);
    contract.validate(value);
}
} // namespace
GameStorage::GameStorage(std::filesystem::path base, std::string application)
    : root_(prepare(base, application)), application_(std::move(application)), lease_(root_) {}
void GameStorage::check() const {
    if (owner_ != std::this_thread::get_id())
        throw std::runtime_error("game.storage: Operation requires its owner thread");
    lease_.check();
}
std::filesystem::path GameStorage::slot_path(std::string_view slot) const {
    if (slot.empty() || slot.size() > 64)
        throw std::runtime_error(
            "game.save: Slot name must contain 1..64 lowercase ASCII letters/digits/_/-");
    for (char c : slot)
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-'))
            throw std::runtime_error("game.save: Invalid slot name");
    return root_ / ("slot-" + std::string(slot) + ".json");
}
void GameStorage::save(std::string_view slot, const GameSave& value,
                       const GameSaveSchema& contract) {
    check();
    const auto path = slot_path(slot);
    schema(contract);
    validate(value, contract);
    const auto bytes = encoded(envelope({{"kind", "save"},
                                         {"application_id", application_},
                                         {"schema", contract.version},
                                         {"scene", value.scene},
                                         {"data", value.data}}),
                               save_limit);
    // Validator ran before any file mutation. Recheck writer ownership after callbacks.
    check();
    asset_storage::replace(path, bytes);
}
GameSave GameStorage::load(std::string_view slot, const GameSaveSchema& contract) const {
    check();
    schema(contract);
    const auto payload = read(slot_path(slot), application_, "save", save_limit);
    const auto& saved_version = payload.at("schema");
    if (!saved_version.is_number_integer() || saved_version < 1 || saved_version > contract.version)
        throw std::runtime_error("game.save: Unsupported save schema version");
    unsigned version = saved_version.get<unsigned>();
    GameSave result{payload.at("scene").get<AssetId>(), payload.at("data")};
    if (!result.data.is_object())
        throw std::runtime_error("game.save: Persistent state must be an object");
    // No implicit disk rewrite and no mutation of an active world during migration.
    if (contract.version - version > 1024)
        throw std::runtime_error("game.save: Migration chain exceeds 1024 steps");
    for (; version < contract.version; ++version) {
        const auto migration = contract.migrations.find(version);
        if (migration == contract.migrations.end() || !migration->second)
            throw std::runtime_error("game.save: Missing migration from schema " +
                                     std::to_string(version));
        result = migration->second(std::move(result));
        if (!result.scene || !result.data.is_object())
            throw std::runtime_error("game.save: Migration returned invalid persistent state");
        (void)encoded(result.data, save_limit);
    }
    validate(result, contract);
    check();
    return result;
}
std::vector<std::string> GameStorage::slots() const {
    check();
    std::vector<std::string> result;
    std::size_t examined = 0;
    for (const auto& entry : std::filesystem::directory_iterator(root_)) {
        if (++examined > 4096)
            throw std::runtime_error("game.save: User directory inventory exceeds 4096 entries");
        const auto name = entry.path().filename().string();
        if (!name.starts_with("slot-") || !name.ends_with(".json"))
            continue;
        const auto slot = name.substr(5, name.size() - 10);
        (void)slot_path(slot);
        asset_storage::ordinary(entry.path());
        if (!entry.is_regular_file())
            throw std::runtime_error("game.save: Slot must be an ordinary file");
        result.push_back(slot);
    }
    std::sort(result.begin(), result.end());
    return result;
}
void GameStorage::erase(std::string_view slot) {
    check();
    const auto path = slot_path(slot);
    asset_storage::ordinary(path);
    if (std::filesystem::exists(path) && !std::filesystem::is_regular_file(path))
        throw std::runtime_error("game.save: Slot must be an ordinary file");
    asset_storage::erase_file(path);
}
void GameStorage::save_settings(const Json& settings,
                                const std::function<void(const Json&)>& validator) {
    check();
    if (!validator || !settings.is_object())
        throw std::runtime_error("game.settings: Settings object and validator required");
    (void)encoded(settings, settings_limit);
    validator(settings);
    const auto bytes = encoded(
        envelope({{"kind", "settings"}, {"application_id", application_}, {"data", settings}}),
        settings_limit);
    check();
    asset_storage::replace(root_ / "settings.json", bytes);
}
Json GameStorage::load_settings(const std::function<void(const Json&)>& validator) const {
    check();
    if (!validator)
        throw std::runtime_error("game.settings: Settings validator required");
    const auto path = root_ / "settings.json";
    asset_storage::ordinary(path);
    auto data = std::filesystem::exists(path)
                    ? read(path, application_, "settings", settings_limit).at("data")
                    : Json::object();
    if (!data.is_object())
        throw std::runtime_error("game.settings: Settings must be an object");
    validator(data);
    check();
    return data;
}
} // namespace forge
