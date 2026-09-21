#include "authored_schema.hpp"
#include "asset_bytes.hpp"
#include "reconstructed_meta.hpp"
#include "reflected_extensions.hpp"
#include "reflected_string.hpp"
#include <algorithm>
#include <array>
#include <forge/animation_components.hpp>
#include <forge/audio_components.hpp>
#include <forge/engine_module.hpp>
#include <forge/model_asset.hpp>
#include <forge/navigation_components.hpp>
#include <forge/ui_components.hpp>
#include <set>
namespace forge::detail {
namespace {
using Json = nlohmann::json;
constexpr const char* admission_name = "forge.authoring.TypeAdmission";
void text(const std::string& value, std::size_t maximum) {
    if (value.size() > maximum || value.find('\0') != std::string::npos)
        throw std::runtime_error("Authoring metadata text exceeds its bounded profile");
    (void)Json(value).dump();
}
template <class T>
ReflectedAdapter reference(flecs::world& world, const char* kind, const char* asset = nullptr) {
    auto type = world.component<T>();
    if (!type.template has<EcsOpaque>())
        type.opaque(flecs::String).serialize([](const flecs::serializer* writer, const T* value) {
            try {
                std::string copy;
                if constexpr (std::is_same_v<T, EntityRef>) {
                    if (bool(value->scene) != bool(value->entity))
                        return -1;
                    copy = (value->scene ? Json(*value) : Json(nullptr)).dump();
                } else
                    copy = value->id ? value->id.str() : std::string{};
                const char* ptr = copy.c_str();
                return writer->value(flecs::String, &ptr);
            } catch (...) {
                return -1;
            }
        });
    return {type.id(), kind, asset,
            [](const void* ptr) -> Json {
                const auto& value = *static_cast<const T*>(ptr);
                if constexpr (std::is_same_v<T, EntityRef>) {
                    if (bool(value.scene) != bool(value.entity))
                        throw std::runtime_error("Partial EntityRef cannot be an authored value");
                    return value.scene ? Json(value) : Json(nullptr);
                } else
                    return value.id ? Json(value.id) : Json(nullptr);
            },
            [](void* ptr, const Json& value) {
                if (value.is_null())
                    *static_cast<T*>(ptr) = {};
                else
                    *static_cast<T*>(ptr) = value.get<T>();
            }};
}
void canonicalize(Json& type) {
    for (auto key : {"display_name", "description", "documentation_url", "read_only"})
        type.erase(key);
    if (type.contains("fields")) {
        for (auto& field : type["fields"])
            canonicalize(field);
        auto& fields = type["fields"];
        std::sort(fields.begin(), fields.end(), [](const auto& a, const auto& b) {
            return a.at("id").template get_ref<const std::string&>() <
                   b.at("id").template get_ref<const std::string&>();
        });
    }
    if (type.contains("element"))
        canonicalize(type["element"]);
    if (type.contains("choices")) {
        for (auto& choice : type["choices"])
            choice.erase("label");
        auto& choices = type["choices"];
        std::sort(choices.begin(), choices.end(),
                  [](const auto& a, const auto& b) { return a.at("value") < b.at("value"); });
    }
}
Json description(flecs::world& world, ecs_entity_t native_type,
                 const AuthoredTypeAdmission& admission,
                 std::span<const ReflectedAdapter> adapters) {
    auto projection = reflected_type_schema(world, native_type, adapters);
    if (projection.at("type") != "struct")
        throw std::runtime_error("An authored component must be a reflected value struct");
    for (const auto& field : projection.at("fields"))
        if (field.at("id") == "$forge")
            throw std::runtime_error("Root field $forge is reserved for authored schema identity");
    // Native reconstruction proves the editor's safe representation is supported.
    // It has no project callbacks even though this worker/runtime world does.
    const ReconstructedMeta reconstructed(world, projection, adapters);
    const ReflectedCandidate candidate(world, native_type, admission.defaults, adapters);
    const auto defaults = read_reflected_native(world, native_type, candidate.data(), adapters);
    if (!reflected_extensions(projection, admission.defaults).is_null())
        throw std::runtime_error("Declared defaults contain unknown properties");
    const auto label = ecs_doc_get_name(world, native_type);
    const auto brief = ecs_doc_get_brief(world, native_type);
    const auto link = ecs_doc_get_link(world, native_type);
    const std::string display = label ? label : admission.key, help = brief ? brief : "",
                      url = link ? link : "";
    text(display, 4095);
    text(help, 4095);
    text(url, 4095);
    return {{"id", admission.key},
            {"module", admission.module},
            {"schema_version", admission.version},
            {"category", admission.category},
            {"display_name", display},
            {"description", help},
            {"documentation_url", url},
            {"structure", projection},
            {"digest", authored_structure_digest(projection)},
            {"defaults", defaults}};
}
} // namespace
std::vector<ReflectedAdapter> authoring_value_adapters(flecs::world& world) {
    std::vector<ReflectedAdapter> refs;
    refs.push_back(reference<EntityRef>(world, "entity_ref"));
#define FORGE_REF(Type) refs.push_back(reference<AssetRef<Type>>(world, "asset_ref", Type::type))
    FORGE_REF(SceneAsset);
    FORGE_REF(PrefabAsset);
    FORGE_REF(ModelAsset);
    FORGE_REF(ModelNodeAsset);
    FORGE_REF(MeshAsset);
    FORGE_REF(MaterialAsset);
    FORGE_REF(TextureAsset);
    FORGE_REF(ShaderAsset);
    FORGE_REF(AudioClipAsset);
    FORGE_REF(SkeletonAsset);
    FORGE_REF(AnimationClipAsset);
    FORGE_REF(NavMeshAsset);
    FORGE_REF(UiDocumentAsset);
#undef FORGE_REF
    auto strings = world.component<std::string>();
    if (!strings.has<EcsOpaque>())
        strings.opaque(reflected_string);
    refs.push_back({strings.id(), "string"});
    return refs;
}
std::string authored_structure_digest(const Json& projection) {
    auto canonical = projection;
    canonicalize(canonical);
    const auto bytes = canonical.dump();
    return asset_detail::content_digest(std::as_bytes(std::span(bytes)));
}
void opt_in_authoring(flecs::world& world, ecs_entity_t type, std::string key, std::string module,
                      std::uint32_t version, Json defaults, std::string category) {
    if (!valid_module_id(key) || key.starts_with("forge.") || !valid_module_id(module) ||
        !version || !ecs_is_alive(world, type))
        throw std::runtime_error("Invalid authoring type identity, owner, version or native type");
    text(category, 255);
    world.component<AuthoredTypeAdmission>(admission_name);
    unsigned count = 0;
    bool duplicate = false;
    world.each([&](flecs::entity existing, const AuthoredTypeAdmission& other) {
        ++count;
        duplicate |= other.key == key || existing.id() == type;
    });
    if (duplicate)
        throw std::runtime_error("Duplicate authoring key or native component opt-in");
    if (count >= 256)
        throw std::runtime_error("Authoring type count exceeds256");
    AuthoredTypeAdmission admission{std::move(key), std::move(module), std::move(category), version,
                                    std::move(defaults)};
    const auto adapters = authoring_value_adapters(world);
    admission.defaults = description(world, type, admission, adapters).at("defaults");
    world.entity(type).set<AuthoredTypeAdmission>(std::move(admission));
    // Ordinary Flecs inheritance/ownership remains authoritative.
    world.entity(type).add(flecs::OnInstantiate, flecs::Inherit);
}
Json export_authored_types(flecs::world& world) {
    world.component<AuthoredTypeAdmission>(admission_name);
    Json result = Json::array();
    std::set<std::string> keys;
    std::size_t bytes = 0;
    std::array<ecs_entity_t, 256> declarations{};
    std::size_t count = 0;
    bool overflow = false;
    world.each([&](flecs::entity type, const AuthoredTypeAdmission&) {
        if (count == declarations.size())
            overflow = true;
        else
            declarations[count++] = type.id();
    });
    if (overflow)
        throw std::runtime_error("Too many authored component declarations");
    if (!count)
        return result;
    const auto adapters = authoring_value_adapters(world);
    for (std::size_t i = 0; i < count; ++i) {
        const auto type = declarations[i];
        const auto admission = world.entity(type).get<AuthoredTypeAdmission>();
        if (result.size() >= 256 || !keys.insert(admission.key).second)
            throw std::runtime_error("Duplicate or excessive authored component declarations");
        auto item = description(world, type, admission, adapters);
        bytes += item.dump().size();
        if (bytes > 16 * 1024 * 1024)
            throw std::runtime_error("Exported authoring metadata exceeds16MiB");
        result.push_back(std::move(item));
    }
    std::sort(result.begin(), result.end(), [](const auto& a, const auto& b) {
        return a.at("id").template get_ref<const std::string&>() <
               b.at("id").template get_ref<const std::string&>();
    });
    return result;
}
void validate_authored_types(flecs::world& world, const Json& copied) {
    if (!copied.is_array() || copied.size() > 256)
        throw std::runtime_error("Invalid copied authoring type list");
    const auto refs = authoring_value_adapters(world);
    std::set<std::string> keys;
    for (const auto& item : copied) {
        const auto key = item.at("id").get<std::string>();
        const auto module = item.at("module").get<std::string>();
        const auto& version = item.at("schema_version");
        if (!valid_module_id(key) || key.starts_with("forge.") || !keys.insert(key).second ||
            !valid_module_id(module) || !version.is_number_integer() ||
            version.get<std::uint64_t>() == 0 || version.get<std::uint64_t>() > UINT32_MAX)
            throw std::runtime_error("Invalid copied authoring identity/version/owner");
        for (const auto name : {"category", "display_name", "description", "documentation_url"})
            text(item.at(name).get_ref<const std::string&>(),
                 std::string_view(name) == "category" ? 255 : 4095);
        const auto& structure = item.at("structure");
        if (structure.at("type") != "struct")
            throw std::runtime_error("Copied component is not a struct");
        for (const auto& field : structure.at("fields"))
            if (field.at("id") == "$forge")
                throw std::runtime_error(
                    "Root field $forge is reserved for authored schema identity");
        const ReconstructedMeta type(world, structure, refs);
        if (item.at("digest") != authored_structure_digest(structure))
            throw std::runtime_error("Copied authored structure digest mismatch");
        const ReflectedCandidate defaults(world, type.native_type(), item.at("defaults"), refs);
        if (read_reflected_native(world, type.native_type(), defaults.data(), refs) !=
            item.at("defaults"))
            throw std::runtime_error("Copied defaults are not canonical native values");
    }
}
} // namespace forge::detail
