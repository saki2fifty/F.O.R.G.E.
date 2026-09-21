#include "authored_migration_document.hpp"
#include "authored_component.hpp"
#include "json_value_equal.hpp"
namespace forge::detail {
AuthoredMigrationDocument::AuthoredMigrationDocument(Json document, Json source, Json target)
    : before_(std::move(document)), source_(std::move(source)), target_(std::move(target)),
      key_(source_.at("id").get<std::string>()) {
    if (source_.at("id") != target_.at("id") || source_.at("module") != target_.at("module") ||
        !source_.at("schema_version").is_number_integer() ||
        !target_.at("schema_version").is_number_integer() ||
        source_.at("schema_version").get<std::uint64_t>() == 0 ||
        target_.at("schema_version").get<std::uint64_t>() > UINT32_MAX ||
        target_.at("schema_version").get<std::uint64_t>() <=
            source_.at("schema_version").get<std::uint64_t>())
        throw std::runtime_error(
            "Document migration requires the same type/owner and a newer schema version");
    if (before_.contains("entities") && !before_.contains("members")) {
        Scene::validate_document(before_);
        rows_ = "entities";
    } else if (before_.contains("members") && !before_.contains("entities")) {
        PrefabDocument::validate(before_);
        rows_ = "members";
    } else {
        throw std::runtime_error("Migration requires one authored scene or prefab document");
    }
    const AuthoredCodec source_codec{0, source_, {}};
    std::size_t bytes = 0;
    for (std::size_t row = 0; row < before_.at(rows_).size(); ++row) {
        const auto& item = before_.at(rows_)[row];
        for (const auto* channel : {"components", "property_overrides"}) {
            if (!item.contains(channel) || !item.at(channel).contains(key_))
                continue;
            const auto& value = item.at(channel).at(key_);
            if (!source_codec.matches(value))
                continue; // Other versions/opaque payloads need their own explicit review.
            const bool partial = std::string_view(channel) == "property_overrides";
            bytes += value.dump().size();
            if (locations_.size() >= 4096 || bytes > 16 * 1024 * 1024)
                throw std::runtime_error("One migration candidate is limited to4096 values/16MiB");
            values_.push_back({{"value", value}, {"property_intent", partial}});
            locations_.push_back({row, partial});
        }
    }
    if (locations_.empty())
        throw std::runtime_error(
            "This document contains no values matching the selected prior schema");
}
Json AuthoredMigrationDocument::candidate(const Json& returned) const {
    if (!returned.is_array() || returned.size() != locations_.size())
        throw std::runtime_error("Migration result count does not match this document candidate");
    auto document = before_;
    const AuthoredCodec target_codec{0, target_, {}};
    for (std::size_t i = 0; i < locations_.size(); ++i) {
        const auto& location = locations_[i];
        const auto& value = returned[i].at("value");
        if (returned[i].at("property_intent").get<bool>() != location.partial ||
            !target_codec.matches(value))
            throw std::runtime_error(
                "Migration result changed schema identity or override ownership");
        auto structure = target_.at("structure");
        if (location.partial) {
            auto& fields = structure["fields"];
            fields.erase(std::remove_if(fields.begin(), fields.end(),
                                        [&](const auto& field) {
                                            return !value.contains(
                                                field.at("id").template get<std::string>());
                                        }),
                         fields.end());
        }
        validate_reflected_json(structure, value);
        document[rows_][location.row][location.partial ? "property_overrides" : "components"]
                [key_] = value;
    }
    return document;
}
void AuthoredMigrationDocument::apply(Scene& scene, const Json& returned,
                                      std::uint64_t revision) const {
    if (rows_ != "entities")
        throw std::runtime_error(
            "Prefab migration must use its own source publication/history boundary");
    if (scene.revision() != revision || !json_value_equal(scene.document(), before_))
        throw std::runtime_error("Scene changed during migration; prepare a fresh candidate");
    const auto schema = scene.schema();
    const auto target_stamp = AuthoredCodec{0, target_, {}}.stamp();
    bool admitted = false;
    for (const auto& type : schema.at("components"))
        if (type.at("id") == key_ && type.value("custom", false))
            admitted = json_value_equal(type.at("admission"), target_stamp);
    if (!admitted)
        throw std::runtime_error(
            "Admit the matching target schema before publishing this migration");
    scene.edit(candidate(returned));
}
} // namespace forge::detail
