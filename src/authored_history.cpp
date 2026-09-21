#include "authored_history.hpp"
#include "asset_bytes.hpp"
#include "bounded_json.hpp"
#include "json_value_equal.hpp"
#include <forge/engine_module.hpp>
#include <forge/project_paths.hpp>
#include <forge/scene.hpp>
#include <set>
namespace forge::detail {
namespace {
using Json = nlohmann::json;
constexpr std::size_t limit = 32 * 1024 * 1024;
std::optional<Json> read_history(const std::filesystem::path& path) {
    if (std::filesystem::weakly_canonical(path) != path)
        throw std::runtime_error("Component history cannot redirect through a link");
    const auto status = std::filesystem::symlink_status(path);
    if (!std::filesystem::exists(status))
        return {};
    if (!std::filesystem::is_regular_file(status))
        throw std::runtime_error("Component history must be an ordinary project file");
    return asset_detail::parse_bounded_json(asset_detail::read_bytes(path, limit), limit, 1000000,
                                            32);
}
std::uint32_t version(const Json& declaration) {
    const auto& value = declaration.at("schema_version");
    if (!value.is_number_integer() || value.get<std::uint64_t>() == 0 ||
        value.get<std::uint64_t>() > UINT32_MAX)
        throw std::runtime_error("Invalid component history schema version");
    return value.get<std::uint32_t>();
}
void identity(const Json& declaration, const std::string& key, const std::string& module) {
    if (declaration.at("id") != key || declaration.at("module") != module)
        throw std::runtime_error("Component history type/owner identity mismatch");
    (void)version(declaration);
    const auto& digest = declaration.at("digest").get_ref<const std::string&>();
    if (digest.size() != 64 || digest.find_first_not_of("0123456789abcdef") != std::string::npos)
        throw std::runtime_error("Invalid component history structure digest");
}
void validate_history(const Json& document) {
    if (!document.is_object() || document.value("format", "") != "forge.component-history" ||
        document.at("version") != 1 || !document.at("types").is_object() ||
        document.at("types").size() > 4096 || document.dump().size() > limit)
        throw std::runtime_error("Invalid/excessive component history; previous file preserved");
    std::size_t declarations = 0;
    for (const auto& [key, record] : document.at("types").items()) {
        const auto module = record.at("module").get<std::string>();
        if (!valid_module_id(key) || key.starts_with("forge.") || !valid_module_id(module) ||
            !record.at("available").is_boolean() || !record.at("schemas").is_array() ||
            record.at("schemas").empty())
            throw std::runtime_error("Invalid component history identity record");
        std::set<std::uint32_t> versions;
        for (const auto& declaration : record.at("schemas")) {
            identity(declaration, key, module);
            if (++declarations > 1024 || !versions.insert(version(declaration)).second)
                throw std::runtime_error("Duplicate or excessive component history versions");
        }
    }
}
} // namespace
AuthoredHistory::AuthoredHistory(std::filesystem::path project)
    : path_(ProjectPaths(std::move(project)).root() / "forge.components.json"),
      baseline_(read_history(path_)),
      document_(baseline_.value_or(
          Json{{"format", "forge.component-history"}, {"version", 1}, {"types", Json::object()}})) {
    validate_history(document_);
}
Json AuthoredHistory::prepare(const Json& manifest) const {
    if (manifest.at("format") != "forge.authored-types" || manifest.at("version") != 1 ||
        manifest.at("profile") != "shared-native-sdk" || !manifest.at("components").is_array() ||
        manifest.at("components").size() > 256)
        throw std::runtime_error("Component history requires a validated SDK inspection manifest");
    auto next = document_;
    for (auto& record : next["types"])
        record["available"] = false;
    std::set<std::string> seen;
    for (const auto& declaration : manifest.at("components")) {
        const auto key = declaration.at("id").get<std::string>();
        const auto module = declaration.at("module").get<std::string>();
        if (!seen.insert(key).second)
            throw std::runtime_error("Duplicate component identity in inspection manifest");
        identity(declaration, key, module);
        auto& record = next["types"][key];
        if (record.is_null())
            record = {{"module", module}, {"available", true}, {"schemas", Json::array()}};
        if (record.at("module") != module)
            throw std::runtime_error("Component key is reserved by its previous module owner: " +
                                     key);
        record["available"] = true;
        bool found = false;
        for (auto& previous : record["schemas"])
            if (version(previous) == version(declaration)) {
                if (previous.at("digest") != declaration.at("digest"))
                    throw std::runtime_error(
                        "Changed component structure requires a new schema version: " + key);
                previous = declaration; // Presentation/default updates do not rewrite scene values.
                found = true;
                break;
            }
        if (!found)
            record["schemas"].push_back(declaration);
    }
    next["last_sdk_fingerprint"] = manifest.at("fingerprint");
    validate_history(next);
    return next;
}
void AuthoredHistory::publish(const Json& prepared) {
    validate_history(prepared);
    const auto current = read_history(path_);
    if (current.has_value() != baseline_.has_value() ||
        (current && !json_value_equal(*current, *baseline_)))
        throw std::runtime_error(
            "Component history changed on disk; inspect again before activation");
    // Prepare every in-memory allocation before committing the single file.
    auto next = prepared;
    std::optional<Json> next_baseline(prepared);
    atomic_write(path_, prepared.dump());
    document_.swap(next);
    baseline_.swap(next_baseline);
}
Json AuthoredHistory::declarations() const {
    auto result = Json::array();
    for (const auto& record : document_.at("types"))
        for (const auto& schema : record.at("schemas"))
            result.push_back(schema);
    return result;
}
} // namespace forge::detail
