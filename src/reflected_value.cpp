#include "reflected_value.hpp"
#include <algorithm>
#include <cmath>
#include <forge/identity.hpp>
#include <limits>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>
namespace forge::detail {
namespace {
using Json = nlohmann::json;
constexpr std::size_t maximum_bytes = 65536, maximum_elements = 4096;
[[noreturn]] void fail(const std::string& path, const char* message) {
    throw std::runtime_error(path + ": " + message);
}
std::string text(const char* value) {
    if (!value)
        return {};
    std::size_t count = 0;
    while (count != 4096 && value[count])
        ++count;
    if (count == 4096)
        fail("reflection", "Metadata text exceeds 4095 bytes");
    return {value, count};
}
void presentation(ecs_world_t* world, ecs_entity_t id, Json& result) {
    result["display_name"] = text(ecs_doc_get_name(world, id));
    result["description"] = text(ecs_doc_get_brief(world, id));
    if (const auto* link = ecs_doc_get_link(world, id))
        result["documentation_url"] = text(link);
}
struct Projection {
    ecs_world_t* world;
    std::span<const ReflectedAdapter> references;
    std::vector<ecs_entity_t> stack;
    unsigned leaves = 0;
    const EcsComponent& storage(ecs_entity_t id) const {
        const auto* value = ecs_get(world, id, EcsComponent);
        if (!value || value->size <= 0 || value->size > int(maximum_bytes) ||
            value->alignment <= 0 || value->alignment > value->size ||
            (value->alignment & (value->alignment - 1)) || value->size % value->alignment)
            fail("reflection", "Invalid or oversized native type layout");
        return *value;
    }
    Json primitive(ecs_entity_t id) {
        const auto* p = ecs_get(world, id, EcsPrimitive);
        if (!p)
            fail("reflection", "Missing native primitive metadata");
        const char* name = nullptr;
        switch (p->kind) {
        case EcsBool:
            name = "bool";
            break;
        case EcsByte:
        case EcsU8:
            name = "uint8";
            break;
        case EcsU16:
            name = "uint16";
            break;
        case EcsU32:
            name = "uint32";
            break;
        case EcsU64:
            name = "uint64";
            break;
        case EcsI8:
            name = "int8";
            break;
        case EcsI16:
            name = "int16";
            break;
        case EcsI32:
            name = "int32";
            break;
        case EcsI64:
            name = "int64";
            break;
        case EcsF32:
            name = "float32";
            break;
        case EcsF64:
            name = "float64";
            break;
        case EcsString:
            name = "string";
            break;
        default:
            fail("reflection",
                 "Pointer-sized, character or process-local ID types are not authored values");
        }
        return {{"type", name}};
    }
    Json describe(ecs_entity_t id, unsigned depth) {
        if (depth > 8 || std::ranges::find(stack, id) != stack.end())
            fail("reflection", "Recursive or excessively nested type");
        const auto& layout = storage(id);
        const auto* type = ecs_get(world, id, EcsType);
        if (!type || type->partial)
            fail("reflection", "Missing or partial native Meta type");
        stack.push_back(id);
        Json result;
        switch (type->kind) {
        case EcsPrimitiveType:
            result = primitive(id);
            break;
        case EcsEnumType:
        case EcsBitmaskType: {
            const bool mask = type->kind == EcsBitmaskType;
            const auto* enumeration = ecs_get(world, id, EcsEnum);
            if (!mask && !enumeration)
                fail("reflection", "Missing enum metadata");
            result = mask ? Json{{"type", "uint32"}} : primitive(enumeration->underlying_type);
            const auto kind = result.at("type").get<std::string>();
            if (!kind.starts_with("int") && !kind.starts_with("uint"))
                fail("reflection", "Enum storage must be an integer");
            result["choices"] = Json::array();
            const auto* constants = ecs_get(world, id, EcsConstants);
            if (!constants || ecs_vec_count(&constants->ordered_constants) > 256)
                fail("reflection", "Missing or excessive enum constants");
            std::uint64_t bits = 0;
            for (int i = 0; i < ecs_vec_count(&constants->ordered_constants); ++i) {
                Json value;
                ecs_entity_t constant = 0;
                if (mask) {
                    const auto& c =
                        ecs_vec_first_t(&constants->ordered_constants, ecs_bitmask_constant_t)[i];
                    value = c.value;
                    bits |= c.value;
                    constant = c.constant;
                } else {
                    const auto& c =
                        ecs_vec_first_t(&constants->ordered_constants, ecs_enum_constant_t)[i];
                    value = kind.starts_with("uint") ? Json(c.value_unsigned) : Json(c.value);
                    constant = c.constant;
                }
                result["choices"].push_back(
                    {{"label", text(ecs_doc_get_name(world, constant))}, {"value", value}});
            }
            if (mask) {
                result["bitmask"] = true;
                result["allowed_bits"] = bits;
            }
            break;
        }
        case EcsStructType: {
            result = {{"type", "struct"}, {"fields", Json::array()}};
            const auto* structure = ecs_get(world, id, EcsStruct);
            if (!structure || ecs_vec_count(&structure->members) <= 0 ||
                ecs_vec_count(&structure->members) > 256)
                fail("reflection", "Missing or excessive struct members");
            std::set<std::string> names;
            std::vector<std::pair<std::size_t, std::size_t>> extents;
            const auto* members = ecs_vec_first_t(&structure->members, ecs_member_t);
            for (int i = 0; i < ecs_vec_count(&structure->members); ++i) {
                const auto& m = members[i];
                const auto& child = storage(m.type);
                const auto member_name = text(m.name);
                if (member_name.empty() || member_name.size() > 255 ||
                    !names.insert(member_name).second || m.offset < 0 || m.count < 0 ||
                    m.count > int(maximum_elements) || m.offset % child.alignment)
                    fail("reflection", "Invalid member name, offset, count or alignment");
                const auto start = std::size_t(m.offset);
                const auto end =
                    start + std::size_t(child.size) * std::size_t(std::max(1, m.count));
                if (end > std::size_t(layout.size))
                    fail(m.name, "Member exceeds native type extent");
                for (const auto& [begin, finish] : extents)
                    if (start < finish && begin < end)
                        fail(m.name, "Reflected members overlap");
                extents.emplace_back(start, end);
                auto field = describe(m.type, depth + 1 + (m.count > 0));
                if (m.count > 0)
                    field = {{"type", "array"}, {"count", m.count}, {"element", std::move(field)}};
                field["id"] = member_name;
                field["serialized"] = true;
                field["read_only"] = false;
                field["animatable"] =
                    field.at("type") == "float32" || field.at("type") == "float64";
                field["unit"] = "unitless";
                const auto key_type =
                    ecs_lookup_path_w_sep(world, 0, reflected_sequence_key_type, "::", "::", false);
                const auto* key = key_type && m.member ? static_cast<const ReflectedSequenceKey*>(
                                                             ecs_get_id(world, m.member, key_type))
                                                       : nullptr;
                if (key) {
                    if ((field.at("type") != "vector" && field.at("type") != "array") ||
                        field.at("element").at("type") != "struct" || key->member.empty() ||
                        key->member.size() > 255)
                        fail(m.name, "Collection entry key requires a named string struct field");
                    bool found = false;
                    for (const auto& f : field.at("element").at("fields"))
                        found |= f.at("id") == key->member && f.at("type") == "string";
                    if (!found)
                        fail(m.name, "Collection entry key is not a reflected string field");
                    field["element_key"] = key->member;
                }
                if (m.member)
                    presentation(world, m.member, field);
                else {
                    field["display_name"] = m.name;
                    field["description"] = "";
                }
                if (m.unit) {
                    const auto* unit = ecs_get(world, m.unit, EcsUnit);
                    if (!unit || !unit->symbol)
                        fail(m.name, "Missing unit symbol");
                    field["unit"] = text(unit->symbol);
                }
                const auto* ranges = m.member ? ecs_get(world, m.member, EcsMemberRanges) : nullptr;
                const EcsMemberRanges native =
                    ranges ? *ranges : EcsMemberRanges{m.range, m.warning_range, m.error_range};
                for (const auto& [key, range] : {std::pair{"value", native.value},
                                                 {"warning_range", native.warning},
                                                 {"error_range", native.error}}) {
                    if (!std::isfinite(range.min) || !std::isfinite(range.max) ||
                        range.min > range.max)
                        fail(m.name, "Invalid native member range");
                    if (range.min == range.max)
                        continue;
                    if (std::string_view(key) == "value") {
                        field["minimum"] = range.min;
                        field["maximum"] = range.max;
                    } else
                        field[key] = {{"minimum", range.min}, {"maximum", range.max}};
                }
                result["fields"].push_back(std::move(field));
            }
            break;
        }
        case EcsArrayType:
        case EcsVectorType: {
            const bool array = type->kind == EcsArrayType;
            const auto* a = ecs_get(world, id, EcsArray);
            const auto* v = ecs_get(world, id, EcsVector);
            if ((array && (!a || a->count <= 0 || a->count > int(maximum_elements))) ||
                (!array && !v))
                fail("reflection", "Invalid collection metadata");
            const auto element = array ? a->type : v->type;
            const auto& child = storage(element);
            if (array &&
                (std::size_t(child.size) * std::size_t(a->count) != std::size_t(layout.size) ||
                 child.alignment != layout.alignment))
                fail("reflection", "Array native layout mismatch");
            if (!array &&
                (layout.size != sizeof(ecs_vec_t) || layout.alignment != alignof(ecs_vec_t)))
                fail("reflection", "Vector must use native Meta lifecycle/storage");
            result = {{"type", array ? "array" : "vector"},
                      {"element", describe(element, depth + 1)}};
            if (array)
                result["count"] = a->count;
            else
                result["maximum_count"] = maximum_elements;
            break;
        }
        case EcsOpaqueType: {
            auto adapter = std::ranges::find(references, id, &ReflectedAdapter::type);
            if (adapter == references.end() || !adapter->kind)
                fail("reflection", "Opaque type requires an explicit engine-owned adapter");
            const std::string kind = adapter->kind;
            if (kind == "vector") {
                const auto* opaque = ecs_get(world, id, EcsOpaque);
                if (!opaque || !ecs_has(world, opaque->as_type, EcsVector) || !opaque->count ||
                    !opaque->serialize_element || !opaque->resize || !opaque->ensure_element)
                    fail("reflection",
                         "Engine vector adapter needs complete native Meta callbacks");
                // Structure comes from native as_type, never from a second type graph.
                auto vector = describe(opaque->as_type, depth);
                stack.pop_back();
                return vector;
            }
            if (kind == "string") {
                const auto* opaque = ecs_get(world, id, EcsOpaque);
                const auto* primitive_type =
                    opaque ? ecs_get(world, opaque->as_type, EcsPrimitive) : nullptr;
                if (!primitive_type || primitive_type->kind != EcsString || !opaque->serialize ||
                    !opaque->assign_string)
                    fail("reflection", "Engine string adapter needs native String callbacks");
                result = {{"type", "string"}};
                break;
            }
            if (kind != "asset_ref" && kind != "entity_ref")
                fail("reflection", "Unknown reference adapter");
            result = {{"type", kind}, {"nullable", true}};
            if (kind == "asset_ref") {
                if (!adapter->asset_type || !*adapter->asset_type)
                    fail("reflection", "AssetRef requires an expected asset type");
                result["asset_type"] = adapter->asset_type;
            }
            break;
        }
        default:
            fail("reflection", "Unsupported native Meta kind");
        }
        if (!result.contains("fields") && !result.contains("element") && ++leaves > 256)
            fail("reflection", "More than 256 reflected leaves");
        stack.pop_back();
        return result;
    }
};
// Check before serialization so an adversarial JSON tree cannot allocate an
// unbounded canonical string. Unknown fields receive the same envelope checks.
struct Envelope {
    std::size_t bytes = 0, elements = 0;
    void add(std::size_t n) {
        if (n > maximum_bytes - bytes)
            fail("value", "Component payload exceeds 64 KiB");
        bytes += n;
    }
    void check(const Json& v, unsigned depth = 0) {
        if (depth > 8)
            fail("value", "Component payload nesting exceeds eight");
        add(1);
        if (v.is_string()) {
            const auto& s = v.get_ref<const std::string&>();
            add(s.size());
            if (s.find('\0') != std::string::npos)
                fail("value", "Embedded NUL is not a reflected string");
        } else if (v.is_array() || v.is_object()) {
            if (v.size() > maximum_elements - elements)
                fail("value", "Component container element budget exceeded");
            elements += v.size();
            for (auto it = v.begin(); it != v.end(); ++it) {
                if (v.is_object())
                    add(it.key().size());
                check(it.value(), depth + 1);
            }
        } else if (v.is_number_float() && !std::isfinite(v.get<double>()))
            fail("value", "Nonfinite component number");
        else if (v.is_binary() || v.is_discarded())
            fail("value", "Unsupported component JSON value");
    }
};
void validate(const Json& field, const Json& value, const std::string& path) {
    const auto kind = field.at("type").get<std::string>();
    if (kind == "struct") {
        if (!value.is_object())
            fail(path, "Expected struct object");
        for (const auto& child : field.at("fields")) {
            const auto name = child.at("id").get<std::string>();
            if (!value.contains(name))
                fail(path + "." + name, "Missing reflected property");
            validate(child, value.at(name), path + "." + name);
        }
    } else if (kind == "array" || kind == "vector") {
        if (!value.is_array() ||
            (kind == "array" && value.size() != field.at("count").get<std::size_t>()) ||
            (kind == "vector" && value.size() > field.at("maximum_count").get<std::size_t>()))
            fail(path, "Collection size/type mismatch");
        auto element = field.at("element");
        // Inline numeric member arrays carry native ranges on their member.
        // Apply its hard value range to every entry; alert bands remain metadata.
        if (field.contains("minimum")) {
            element["minimum"] = field.at("minimum");
            element["maximum"] = field.at("maximum");
        }
        for (std::size_t i = 0; i < value.size(); ++i)
            validate(element, value[i], path + "[" + std::to_string(i) + "]");
        if (field.contains("element_key")) {
            const auto key = field.at("element_key").get<std::string>();
            std::set<std::string> keys;
            for (const auto& item : value) {
                const auto& id = item.at(key).get_ref<const std::string&>();
                if (id.empty() || !keys.insert(id).second)
                    fail(path, "Empty or duplicate collection entry key");
            }
        }
    } else if (kind == "asset_ref" || kind == "entity_ref") {
        if (value.is_null()) {
            if (!field.value("nullable", false))
                fail(path, "Reference is required");
        } else if (kind == "asset_ref")
            (void)value.get<AssetId>();
        else
            (void)value.get<EntityRef>();
    } else if (kind == "bool") {
        if (!value.is_boolean())
            fail(path, "Expected boolean");
    } else if (kind == "string") {
        if (!value.is_string())
            fail(path, "Expected UTF-8 string");
    } else if (kind.starts_with("int") || kind.starts_with("uint")) {
        if (!value.is_number_integer())
            fail(path, "Expected exact integer");
        const bool unsign = kind.starts_with("uint");
        const unsigned bits = unsigned(std::stoul(kind.substr(unsign ? 4 : 3)));
        if (bits != 8 && bits != 16 && bits != 32 && bits != 64)
            fail(path, "Unsupported integer width");
        if (unsign) {
            if (!value.is_number_unsigned() && value.get<std::int64_t>() < 0)
                fail(path, "Negative unsigned value");
            const auto n = value.get<std::uint64_t>();
            const auto maximum = bits == 64 ? UINT64_MAX : (std::uint64_t{1} << bits) - 1;
            if (n > maximum)
                fail(path, "Unsigned integer overflow");
            if (field.contains("minimum")) {
                const double low = std::ceil(field.at("minimum").get<double>()),
                             high = std::floor(field.at("maximum").get<double>());
                // Cast bounds, never the integer to double. 2^64 itself is not castable.
                if (low >= 0x1p64 || (low > 0 && n < std::uint64_t(low)) || high < 0 ||
                    (high < 0x1p64 && n > std::uint64_t(high)))
                    fail(path, "Integer outside native member range");
            }
        } else {
            if (value.is_number_unsigned() && value.get<std::uint64_t>() > std::uint64_t(INT64_MAX))
                fail(path, "Signed integer overflow");
            const auto n = value.get<std::int64_t>();
            const auto low = bits == 64 ? INT64_MIN : -(std::int64_t{1} << (bits - 1));
            const auto high = bits == 64 ? INT64_MAX : (std::int64_t{1} << (bits - 1)) - 1;
            if (n < low || n > high)
                fail(path, "Signed integer overflow");
            if (field.contains("minimum")) {
                const double minimum = std::ceil(field.at("minimum").get<double>()),
                             maximum = std::floor(field.at("maximum").get<double>());
                if (minimum >= 0x1p63 || (minimum > -0x1p63 && n < std::int64_t(minimum)) ||
                    maximum < -0x1p63 || (maximum < 0x1p63 && n > std::int64_t(maximum)))
                    fail(path, "Integer outside native member range");
            }
        }
        if (field.value("bitmask", false)) {
            if (value.get<std::uint64_t>() & ~field.at("allowed_bits").get<std::uint64_t>())
                fail(path, "Unknown bitmask bits");
        } else if (field.contains("choices")) {
            bool found = false;
            for (const auto& choice : field.at("choices"))
                found |= choice.at("value") == value;
            if (!found)
                fail(path, "Unknown enum value");
        }
    } else if (kind == "float32" || kind == "float64") {
        if (!value.is_number())
            fail(path, "Expected number");
        const double n = value.get<double>();
        const bool single = kind == "float32";
        const double maximum =
            single ? double(std::numeric_limits<float>::max()) : std::numeric_limits<double>::max();
        if (!std::isfinite(n) || n < -maximum || n > maximum)
            fail(path, "Floating point overflow/nonfinite value");
        const double stored = single ? double(float(n)) : n;
        // Preserve the existing builtin f32 range convention at representable endpoints.
        double low = field.value("minimum", -maximum);
        if (single && low >= -maximum && low <= maximum)
            low = double(float(low));
        if (stored < low || stored > field.value("maximum", maximum))
            fail(path, "Component field outside supported range");
    } else
        fail(path, "Unsupported reflected value kind");
}
} // namespace
nlohmann::json reflected_type_schema(flecs::world world, ecs_entity_t type,
                                     std::span<const ReflectedAdapter> references) {
    Projection projection{world.c_ptr(), references, {}, 0};
    auto result = projection.describe(type, 0);
    if (result.dump().size() > maximum_bytes)
        fail("reflection", "Metadata projection exceeds 64 KiB");
    return result;
}
void validate_reflected_json(const Json& schema, const Json& value) {
    Envelope{}.check(value);
    if (value.dump().size() > maximum_bytes)
        fail("value", "Canonical component payload exceeds 64 KiB");
    validate(schema, value, schema.value("id", std::string("component")));
}
} // namespace forge::detail
