#include "authored_component.hpp"
#include "authored_schema.hpp"
#include "reconstructed_meta.hpp"
#include "reflected_extensions.hpp"
#include <forge/world.hpp>
#include <memory>
namespace forge::detail {
using Json = nlohmann::json;
std::string AuthoredCodec::key() const { return declaration.at("id").get<std::string>(); }
Json AuthoredCodec::stamp() const {
    return {{"format", "forge.authored-component"},
            {"version", 1u},
            {"module", declaration.at("module")},
            {"schema_version", declaration.at("schema_version")},
            {"digest", declaration.at("digest")}};
}
bool AuthoredCodec::matches(const Json& value) const {
    return value.is_object() && value.contains("$forge") && value.at("$forge") == stamp();
}
Json AuthoredCodec::defaults() const {
    auto value = declaration.at("defaults");
    value["$forge"] = stamp();
    return value;
}
Json AuthoredCodec::editor_schema() const {
    auto result = declaration;
    result["custom"] = true;
    result["optional"] = true;
    result["admission"] = stamp();
    result["fields"] = declaration.at("structure").at("fields");
    for (auto& field : result["fields"])
        field["default"] = declaration.at("defaults").at(field.at("id").get<std::string>());
    result.erase("structure");
    return result;
}
std::optional<ReflectedCandidate> AuthoredCodec::prepare(flecs::world& world,
                                                         const Json& value) const {
    if (!matches(value))
        return std::nullopt;
    return ReflectedCandidate(world, native_type, value, adapters);
}
Json AuthoredCodec::read(flecs::entity entity, bool effective) const {
    if (!(effective ? entity.has(native_type) : entity.owns(native_type)))
        return nullptr;
    const auto* value = ecs_get_id(entity.world(), entity, native_type);
    auto result = read_reflected_native(entity.world(), native_type, value, adapters);
    result["$forge"] = stamp();
    return result;
}
void AuthoredCodec::apply(flecs::entity entity,
                          const std::optional<ReflectedCandidate>& value) const {
    if (!value) {
        if (entity.owns(native_type))
            entity.remove(native_type);
        return;
    }
    const auto next = read_reflected_native(entity.world(), native_type, value->data(), adapters);
    if (entity.owns(native_type) &&
        read_reflected_native(entity.world(), native_type,
                              ecs_get_id(entity.world(), entity, native_type), adapters) == next)
        return;
    // Native copy hooks retain owned strings/vectors. No bytes cross processes.
    ecs_set_id(entity.world(), entity, native_type,
               std::size_t(ecs_get_type_info(entity.world(), native_type)->size), value->data());
}
Json AuthoredCodec::extensions(const Json& value) const {
    return reflected_extensions(declaration.at("structure"), value);
}
Json AuthoredCodec::merge(const Json& known, const Json& opaque) const {
    auto result = merge_reflected_extensions(declaration.at("structure"), known, opaque);
    result["$forge"] = stamp();
    return result;
}
std::vector<AuthoredCodec> authored_codecs(flecs::world& world) {
    const auto declarations = export_authored_types(world);
    if (declarations.empty())
        return {};
    const auto adapters = authoring_value_adapters(world);
    std::vector<AuthoredCodec> result;
    for (const auto& declaration : declarations) {
        ecs_entity_t native = 0;
        world.each([&](flecs::entity type, const AuthoredTypeAdmission& admission) {
            if (declaration.at("id") == admission.key)
                native = type;
        });
        if (!native)
            throw std::runtime_error("Admitted native type disappeared during schema publication");
        result.push_back({native, declaration, adapters});
    }
    return result;
}
EngineModule copied_authoring_module(Json copied) {
    EngineModule module;
    module.id = "forge.authored_inspection";
    module.dependencies = {"forge.transforms"};
    module.schema_roles = role_mask(WorldRole::Authoring) | role_mask(WorldRole::Validation);
    module.schemas = [copied = std::move(copied)](ModuleContext& context) {
        auto& world = context.world;
        try {
            validate_authored_types(world, copied);
        } catch (const std::exception& e) {
            throw std::runtime_error(std::string("Copied manifest validation: ") + e.what());
        }
        const auto refs = authoring_value_adapters(world);
        std::vector<std::unique_ptr<ReconstructedMeta>> prepared;
        for (const auto& declaration : copied) {
            auto type =
                std::make_unique<ReconstructedMeta>(world, declaration.at("structure"), refs);
            const auto native = type->native_type();
            ecs_doc_set_name(world, native,
                             declaration.at("display_name").get_ref<const std::string&>().c_str());
            ecs_doc_set_brief(world, native,
                              declaration.at("description").get_ref<const std::string&>().c_str());
            ecs_doc_set_link(
                world, native,
                declaration.at("documentation_url").get_ref<const std::string&>().c_str());
            try {
                opt_in_authoring(world, native, declaration.at("id"), declaration.at("module"),
                                 declaration.at("schema_version"), declaration.at("defaults"),
                                 declaration.at("category"));
            } catch (const std::exception& e) {
                throw std::runtime_error(std::string("Reconstructed type admission: ") + e.what());
            }
            prepared.push_back(std::move(type));
        }
        for (auto& type : prepared)
            type->release_to_world();
    };
    return module;
}
void validate_custom_values(const Json& schema, const Json& components) {
    const auto components_schema = schema.find("components");
    if (components_schema == schema.end())
        return;
    // Borrow the admitted schema; copying every descriptor per entity makes
    // simple edits scale with unrelated component metadata.
    for (const auto& type : *components_schema) {
        const auto key = type.at("id").get<std::string>();
        if (!type.value("custom", false) || !components.contains(key))
            continue;
        const auto& value = components.at(key);
        if (value.is_object() && value.value("$forge", Json()) == type.at("admission"))
            validate_reflected_json({{"type", "struct"}, {"fields", type.at("fields")}}, value);
    }
}
void validate_runtime_custom_values(const WorldContext& context, const Json& components) {
    if (context.role() != WorldRole::Runtime)
        return;
    for (const auto& [key, value] : components.items()) {
        const auto& codecs = context.authored_codecs();
        const auto found = std::find_if(codecs.begin(), codecs.end(),
                                        [&](const auto& codec) { return codec.key() == key; });
        const bool marked =
            value.is_object() && value.contains("$forge") && value.at("$forge").is_object() &&
            value.at("$forge").value("format", Json()) == "forge.authored-component";
        if ((found != codecs.end() && !found->matches(value)) || (marked && found == codecs.end()))
            throw std::runtime_error(
                "Runtime component " + key +
                " requires its matching authored schema; preserved data was not loaded");
    }
}
void project_custom_prefab_intent(Json& values, const Json& instance, const Json& schema) {
    const auto intent = instance.value("property_overrides", Json::object());
    const auto components_schema = schema.find("components");
    if (components_schema == schema.end())
        return;
    // Borrow the admitted schema; copying every descriptor per entity makes
    // simple edits scale with unrelated component metadata.
    for (const auto& type : *components_schema) {
        const auto key = type.at("id").get<std::string>();
        if (!type.value("custom", false) || !intent.contains(key))
            continue;
        const auto& fields = intent.at(key);
        if (!fields.is_object() || fields.value("$forge", Json()) != type.at("admission"))
            continue; // Unknown schema intent remains opaque and unchanged.
        if (instance.at("components").contains(key))
            throw std::runtime_error("Conflicting custom component/property override intent");
        if (values.contains(key)) {
            if (!values.at(key).is_object() ||
                values.at(key).value("$forge", Json()) != type.at("admission"))
                continue; // Never interpret overrides against an incompatible base.
        } else {
            values[key] = type.at("defaults");
            values[key]["$forge"] = type.at("admission");
        }
        for (const auto& field : type.at("fields")) {
            const auto name = field.at("id").get<std::string>();
            if (fields.contains(name))
                values[key][name] = fields.at(name);
        }
    }
    validate_custom_values(schema, values);
}
} // namespace forge::detail
