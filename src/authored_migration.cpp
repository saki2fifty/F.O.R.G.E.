#include "authored_migration.hpp"
#include "authored_component.hpp"
#include "authored_schema.hpp"
#include "json_value_equal.hpp"
#include <set>
namespace forge::detail {
namespace {
using Json = nlohmann::json;
using Path = std::vector<std::string>;
const Json* field(const Json& type, const std::string& id) {
    if (type.at("type") != "struct")
        return nullptr;
    for (const auto& member : type.at("fields"))
        if (member.at("id") == id)
            return &member;
    return nullptr;
}
Path path(const Json& input) {
    if (!input.is_array() || input.empty() || input.size() > 16)
        throw std::runtime_error("Migration field path must contain1–16 segments");
    Path result;
    for (const auto& part : input) {
        if (!part.is_string() || part.get_ref<const std::string&>().empty() ||
            part.get_ref<const std::string&>().size() > 255)
            throw std::runtime_error("Invalid migration field path segment");
        result.push_back(part.get<std::string>());
    }
    return result;
}
const Json& resolve(const Json& root, const Path& path) {
    auto* current = &root;
    for (const auto& part : path) {
        const auto kind = current->at("type").get<std::string>();
        if (kind == "array" || kind == "vector") {
            if (part != "*")
                throw std::runtime_error("Collection migration paths require an element wildcard");
            current = &current->at("element");
        } else if (const auto* child = field(*current, part)) {
            current = child;
        } else {
            throw std::runtime_error("Migration path is absent from admitted native metadata");
        }
    }
    return *current;
}
Json stamp(const Json& declaration) { return AuthoredCodec{0, declaration, {}}.stamp(); }
void validate_value(const Json& declaration, const Json& value, bool partial) {
    if (!value.is_object() || !value.contains("$forge") ||
        !json_value_equal(value.at("$forge"), stamp(declaration)))
        throw std::runtime_error("Migration source/target component identity does not match");
    auto structure = declaration.at("structure");
    if (partial) {
        auto& fields = structure["fields"];
        fields.erase(std::remove_if(fields.begin(), fields.end(),
                                    [&](const auto& f) {
                                        return !value.contains(
                                            f.at("id").template get<std::string>());
                                    }),
                     fields.end());
    }
    validate_reflected_json(structure, value);
}
} // namespace
AuthoredMigration::AuthoredMigration(flecs::world& world, Json source, Json target,
                                     const Json& rules)
    : source_(std::move(source)), target_(std::move(target)) {
    validate_authored_types(world, Json::array({source_}));
    validate_authored_types(world, Json::array({target_}));
    if (source_.at("id") != target_.at("id") || source_.at("module") != target_.at("module") ||
        target_.at("schema_version").get<std::uint32_t>() <=
            source_.at("schema_version").get<std::uint32_t>())
        throw std::runtime_error(
            "Migration requires the same immutable type/owner and a newer version");
    if (!rules.is_object() || rules.size() > 2 || rules.dump().size() > 65536 ||
        !rules.value("aliases", Json::array()).is_array() ||
        !rules.value("defaults", Json::array()).is_array())
        throw std::runtime_error("Invalid or excessive migration rules");
    for (const auto& [key, value] : rules.items())
        if (key != "aliases" && key != "defaults")
            throw std::runtime_error("Unknown migration rule");
    const auto aliases = rules.value("aliases", Json::array());
    const auto defaults = rules.value("defaults", Json::array());
    if (aliases.size() + defaults.size() > 256)
        throw std::runtime_error("Migration exceeds256 explicit rules");
    for (const auto& rule : aliases) {
        const auto from = path(rule.at("path"));
        const auto name = rule.at("name").get<std::string>();
        if (rule.size() != 2 || name.empty() || name.size() > 255 ||
            (from.size() == 1 && name == "$forge") || name == from.back() ||
            !aliases_.emplace(from, name).second)
            throw std::runtime_error("Invalid, duplicate or redundant field rename");
        // A collection element is not itself a named member.
        auto parent = from;
        parent.pop_back();
        if (!field(resolve(source_.at("structure"), parent), from.back()))
            throw std::runtime_error("Only reflected struct members may be renamed");
    }
    std::set<Path> destinations;
    for (const auto& [from, name] : aliases_) {
        Path destination, prefix;
        for (const auto& part : from) {
            prefix.push_back(part);
            auto rename = aliases_.find(prefix);
            destination.push_back(rename == aliases_.end() ? part : rename->second);
        }
        (void)resolve(target_.at("structure"), destination);
        if (!destinations.insert(destination).second)
            throw std::runtime_error("Ambiguous migration rename destination");
        auto next = from;
        std::set<Path> seen;
        while (aliases_.contains(next)) {
            if (!seen.insert(next).second)
                throw std::runtime_error("Cyclic migration aliases");
            next.back() = aliases_.at(next);
        }
    }
    for (const auto& rule : defaults) {
        const auto target_path = path(rule.at("path"));
        if (rule.size() != 2 || !defaults_.emplace(target_path, rule.at("value")).second)
            throw std::runtime_error("Duplicate or invalid explicit migration default");
        validate_reflected_json(resolve(target_.at("structure"), target_path), rule.at("value"));
    }
}
AuthoredMigration::Json AuthoredMigration::convert(const Json& from, const Json& to,
                                                   const Json& value, const Json* defaults,
                                                   Path source_path, Path target_path,
                                                   bool partial) const {
    const auto kind = from.at("type").get<std::string>();
    if (kind != to.at("type").get<std::string>() ||
        !json_value_equal(from.value("unit", Json()), to.value("unit", Json())) ||
        !json_value_equal(from.value("asset_type", Json()), to.value("asset_type", Json())))
        throw std::runtime_error(
            "Migration cannot implicitly change field representation or units");
    if (kind == "struct") {
        auto result = value;
        std::map<std::string, std::string> moves;
        std::set<std::string> destinations;
        for (const auto& old : from.at("fields")) {
            const auto id = old.at("id").get<std::string>();
            auto next_path = source_path;
            next_path.push_back(id);
            const auto alias = aliases_.find(next_path);
            const auto name = alias == aliases_.end() ? id : alias->second;
            if (field(to, name)) {
                if (!destinations.insert(name).second)
                    throw std::runtime_error("Migration maps multiple fields to one destination");
                moves.emplace(id, name);
            }
        }
        // Simultaneous rename permits a forward chain, but never overwrites an
        // opaque or removed member merely because the target now recognizes it.
        for (const auto& [id, name] : moves) {
            if (id != name && value.contains(name) && !moves.contains(name))
                throw std::runtime_error("Migration rename collides with retained unknown data");
            if (id != name)
                result.erase(id);
        }
        for (const auto& [id, name] : moves) {
            if (!value.contains(id))
                continue;
            auto next_source = source_path, next_target = target_path;
            next_source.push_back(id);
            next_target.push_back(name);
            const Json* child_default =
                defaults && defaults->is_object() && defaults->contains(name) ? &defaults->at(name)
                                                                              : nullptr;
            result[name] = convert(*field(from, id), *field(to, name), value.at(id), child_default,
                                   std::move(next_source), std::move(next_target), false);
        }
        for (const auto& next : to.at("fields")) {
            const auto id = next.at("id").get<std::string>();
            if (destinations.contains(id))
                continue;
            if (value.contains(id))
                throw std::runtime_error("Migration new field collides with retained unknown data");
            if (partial)
                continue; // Adding a field must not invent instance override intent.
            auto next_path = target_path;
            next_path.push_back(id);
            const auto declared = defaults_.find(next_path);
            if (declared != defaults_.end())
                result[id] = declared->second;
            else if (defaults && defaults->is_object() && defaults->contains(id))
                result[id] = defaults->at(id);
            else
                throw std::runtime_error(
                    "New collection member needs an explicit versioned migration default");
        }
        return result;
    }
    if (kind == "array" || kind == "vector") {
        auto result = value;
        source_path.push_back("*");
        target_path.push_back("*");
        for (std::size_t i = 0; i < value.size(); ++i) {
            const auto* item_default =
                kind == "array" && defaults && defaults->is_array() && i < defaults->size()
                    ? &defaults->at(i)
                    : nullptr;
            result[i] = convert(from.at("element"), to.at("element"), value.at(i), item_default,
                                source_path, target_path, false);
        }
        return result;
    }
    // Values are retained exactly; the target's ranges/enum/bitmask/count checks
    // run over the complete candidate before it can leave the worker.
    return value;
}
AuthoredMigration::Json AuthoredMigration::migrate(const Json& value, bool partial) const {
    validate_value(source_, value, partial);
    auto result = convert(source_.at("structure"), target_.at("structure"), value,
                          &target_.at("defaults"), {}, {}, partial);
    result["$forge"] = stamp(target_);
    validate_value(target_, result, partial);
    return result;
}
} // namespace forge::detail
