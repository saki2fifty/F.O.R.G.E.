#include "reconstructed_meta.hpp"
#include <cmath>
#include <map>
#include <set>
namespace forge::detail {
namespace {
using Json = nlohmann::json;
void require(bool value, const char* why) {
    if (!value)
        throw std::runtime_error(why);
}
std::string bounded_text(const Json& value, const char* key, std::size_t limit = 4095) {
    const auto found = value.find(key);
    if (found == value.end())
        return {};
    require(found->is_string(), "Copied metadata text must be a string");
    const auto& result = found->get_ref<const std::string&>();
    require(result.size() <= limit && result.find('\0') == std::string::npos,
            "Copied metadata text exceeds bounds or contains NUL");
    (void)Json(result).dump(); // Enforce valid UTF-8 before passing native C strings.
    return result;
}
// Check the copied envelope before native registration or re-serialization.
// Metadata is a larger envelope than a component value: labels and constants
// belong here, while values retain their separate 64-KiB limit.
void bounded_projection(const Json& value, std::size_t& bytes, unsigned& nodes,
                        unsigned depth = 0) {
    require(depth <= 32 && ++nodes <= 32768, "Copied metadata exceeds tree bounds");
    const auto add = [&](std::size_t count) {
        require(count <= 1024 * 1024 - bytes, "Copied metadata exceeds 1 MiB");
        bytes += count;
    };
    add(32); // Includes punctuation and the longest JSON scalar representation.
    if (value.is_string()) {
        const auto size = value.get_ref<const std::string&>().size();
        require(size <= 1024 * 1024 / 6, "Copied metadata string exceeds bounds");
        add(size * 6); // Worst-case JSON escaping; no allocation required.
    } else if (value.is_structured()) {
        require(value.size() <= 32768, "Copied metadata container exceeds bounds");
        for (auto it = value.begin(); it != value.end(); ++it) {
            if (value.is_object()) {
                require(it.key().size() <= 255, "Copied metadata key exceeds bounds");
                add(it.key().size() * 6 + 3);
            }
            bounded_projection(it.value(), bytes, nodes, depth + 1);
        }
    } else {
        require(!value.is_binary() && !value.is_discarded() &&
                    (!value.is_number_float() || std::isfinite(value.get<double>())),
                "Invalid copied metadata scalar");
    }
}
void consistent_ranges(const EcsMemberRanges& ranges) {
    const auto contained = [](auto inner, auto outer) {
        return inner.min == inner.max || outer.min == outer.max ||
               (inner.min >= outer.min && inner.max <= outer.max);
    };
    // Match pinned struct_ts.c admission. Alert thresholds stay diagnostic;
    // consistency of metadata is distinct from rejecting component values.
    require(contained(ranges.error, ranges.value) && contained(ranges.warning, ranges.value) &&
                contained(ranges.warning, ranges.error),
            "Copied warning/error bands exceed their containing native range");
}
struct Builder {
    flecs::world world;
    ecs_entity_t scope;
    std::span<const ReflectedAdapter> refs;
    unsigned nodes = 0, leaves = 0;
    ecs_entity_t entity() { return world.entity().child_of(scope); }
    ecs_entity_t primitive(const std::string& kind) {
        static const std::map<std::string, ecs_entity_t> types{
            {"bool", flecs::Bool},   {"int8", flecs::I8},     {"int16", flecs::I16},
            {"int32", flecs::I32},   {"int64", flecs::I64},   {"uint8", flecs::U8},
            {"uint16", flecs::U16},  {"uint32", flecs::U32},  {"uint64", flecs::U64},
            {"float32", flecs::F32}, {"float64", flecs::F64}, {"string", flecs::String}};
        const auto found = types.find(kind);
        require(found != types.end(), "Unsupported copied native primitive");
        return found->second;
    }
    const EcsComponent& layout(ecs_entity_t type) {
        const auto* result = ecs_get(world.c_ptr(), type, EcsComponent);
        require(result && result->size > 0 && result->size <= 65536 && result->alignment > 0 &&
                    !(result->alignment & (result->alignment - 1)),
                "Reconstructed native layout exceeds the component profile");
        return *result;
    }
    void docs(ecs_entity_t entity, const Json& source) {
        const auto label = bounded_text(source, "display_name");
        const auto description = bounded_text(source, "description");
        if (source.contains("display_name"))
            ecs_doc_set_name(world, entity, label.c_str());
        ecs_doc_set_brief(world, entity, description.c_str());
        if (source.contains("documentation_url")) {
            const auto link = bounded_text(source, "documentation_url");
            ecs_doc_set_link(world, entity, link.c_str());
        }
    }
    ecs_member_value_range_t range(const Json& source) {
        if (!source.contains("minimum") && !source.contains("maximum"))
            return {};
        const double lo = source.at("minimum").get<double>(),
                     hi = source.at("maximum").get<double>();
        require(std::isfinite(lo) && std::isfinite(hi) && lo < hi,
                "Invalid copied native member range");
        return {lo, hi};
    }
    ecs_entity_t make(const Json& source, unsigned depth = 0) {
        require(source.is_object() && depth <= 8 && ++nodes <= 4096,
                "Copied native metadata exceeds depth/node bounds");
        const auto kind = bounded_text(source, "type", 32);
        if (kind == "struct") {
            const auto& fields = source.at("fields");
            require(fields.is_array() && !fields.empty() && fields.size() <= 256,
                    "Copied struct field count exceeds bounds");
            struct Field {
                std::string name;
                ecs_entity_t type;
                const Json* source;
            };
            std::vector<Field> prepared;
            std::set<std::string> names;
            std::size_t size = 0, alignment = 1;
            for (const auto& field : fields) {
                const auto name = bounded_text(field, "id", 255);
                require(!name.empty() && names.insert(name).second,
                        "Missing or duplicate copied member key");
                const auto type = make(field, depth + 1);
                const auto& storage = layout(type);
                alignment = std::max(alignment, std::size_t(storage.alignment));
                size = (size + storage.alignment - 1) & ~(std::size_t(storage.alignment) - 1);
                size += storage.size;
                require(size <= 65536, "Copied struct storage exceeds 64 KiB");
                prepared.push_back({name, type, &field});
            }
            size = (size + alignment - 1) & ~(alignment - 1);
            require(size <= 65536, "Copied struct aligned storage exceeds 64 KiB");
            const auto type = entity();
            for (const auto& field : prepared) {
                ecs_entity_t unit = 0;
                const auto symbol = bounded_text(*field.source, "unit");
                if (!symbol.empty() && symbol != "unitless") {
                    ecs_unit_desc_t description{};
                    description.entity = entity();
                    description.symbol = symbol.c_str();
                    unit = ecs_unit_init(world, &description);
                    require(unit != 0, "Copied unit metadata registration failed");
                }
                // Physical declaration order plus native automatic offsets avoid
                // the pinned explicit-zero-offset/member-entity mismatch.
                auto member = world.entity().child_of(type).set_name(field.name.c_str());
                EcsMember metadata{};
                metadata.type = field.type;
                metadata.unit = unit;
                member.set<EcsMember>(metadata);
                docs(member, *field.source);
                EcsMemberRanges ranges{};
                ranges.value = range(*field.source);
                if (field.source->contains("warning_range"))
                    ranges.warning = range(field.source->at("warning_range"));
                if (field.source->contains("error_range"))
                    ranges.error = range(field.source->at("error_range"));
                consistent_ranges(ranges);
                if (ranges.value.min != ranges.value.max ||
                    ranges.warning.min != ranges.warning.max ||
                    ranges.error.min != ranges.error.max)
                    member.set<EcsMemberRanges>(ranges);
                if (field.source->contains("element_key")) {
                    const auto key = bounded_text(*field.source, "element_key", 255);
                    world.component<ReflectedSequenceKey>(reflected_sequence_key_type);
                    member.set<ReflectedSequenceKey>({key});
                }
            }
            const auto& actual = layout(type);
            require(std::size_t(actual.size) == size && std::size_t(actual.alignment) == alignment,
                    "Native reconstructed struct layout differs from checked layout");
            return type;
        }
        if (kind == "array" || kind == "vector") {
            const auto element = make(source.at("element"), depth + 1);
            const auto type = entity();
            if (kind == "array") {
                require(source.at("count").is_number_integer(),
                        "Copied array count must be an integer");
                const auto count = source.at("count").get<std::int64_t>();
                require(count > 0 && count <= 4096 &&
                            std::uint64_t(layout(element).size) * count <= 65536,
                        "Copied fixed array count/storage exceeds bounds");
                ecs_array_desc_t desc{};
                desc.entity = type;
                desc.type = element;
                desc.count = int(count);
                require(ecs_array_init(world, &desc) == type, "Native array reconstruction failed");
            } else {
                require(source.at("maximum_count") == 4096,
                        "Copied vector profile differs from native projection");
                ecs_vector_desc_t desc{};
                desc.entity = type;
                desc.type = element;
                require(ecs_vector_init(world, &desc) == type,
                        "Native vector reconstruction failed");
            }
            (void)layout(type);
            return type;
        }
        require(++leaves <= 256, "Copied schema has more than 256 reflected leaves");
        if (kind == "asset_ref" || kind == "entity_ref") {
            const auto expected =
                kind == "asset_ref" ? bounded_text(source, "asset_type", 255) : std::string{};
            for (const auto& adapter : refs)
                if (adapter.kind && kind == adapter.kind &&
                    (kind == "entity_ref" ||
                     (adapter.asset_type && expected == adapter.asset_type)))
                    return adapter.type;
            throw std::runtime_error("Copied reference requires its explicit engine adapter");
        }
        const auto underlying = primitive(kind);
        if (!source.contains("choices"))
            return underlying;
        require(kind.starts_with("int") || kind.starts_with("uint"),
                "Copied constants require fixed-width integer storage");
        const bool mask = source.value("bitmask", false);
        require(!mask || kind == "uint32", "Native bitmask storage must be uint32");
        const auto& choices = source.at("choices");
        require(choices.is_array() && !choices.empty() && choices.size() <= 256,
                "Copied enum constant count exceeds bounds");
        std::set<Json> distinct;
        for (const auto& choice : choices) {
            (void)bounded_text(choice, "label");
            validate_reflected_json({{"type", kind}}, choice.at("value"));
            require(distinct.insert(choice.at("value")).second, "Duplicate copied constant value");
        }
        const auto type = entity();
        if (mask)
            ecs_add_id(world, type, ecs_id(EcsBitmask));
        else {
            const EcsEnum data{underlying};
            ecs_set_id(world, type, ecs_id(EcsEnum), sizeof(data), &data);
        }
        unsigned index = 0;
        for (const auto& choice : choices) {
            auto constant =
                world.entity().child_of(type).set_name(("c" + std::to_string(index++)).c_str());
            const ReflectedCandidate value(world, underlying, choice.at("value"));
            ecs_set_id(world, constant, ecs_pair(EcsConstant, underlying), layout(underlying).size,
                       value.data());
            const auto label = bounded_text(choice, "label");
            ecs_doc_set_name(world, constant, label.c_str());
        }
        (void)layout(type);
        return type;
    }
};
} // namespace
ReconstructedMeta::ReconstructedMeta(flecs::world world, const nlohmann::json& projection,
                                     std::span<const ReflectedAdapter> references)
    : world_(world), scope_(world.entity()) {
    try {
        std::size_t bytes = 0;
        unsigned nodes = 0;
        bounded_projection(projection, bytes, nodes);
        Builder builder{world, scope_, references};
        type_ = builder.make(projection);
        // Re-project using native metadata before any values can use this type.
        // Malformed collection-key or reference annotations fail here as well.
        require(reflected_type_schema(world, type_, references) == projection,
                "Reconstructed native metadata differs from the admitted projection");
    } catch (...) {
        ecs_delete(world_, scope_);
        throw;
    }
}
ReconstructedMeta::~ReconstructedMeta() { ecs_delete(world_, scope_); }
} // namespace forge::detail
