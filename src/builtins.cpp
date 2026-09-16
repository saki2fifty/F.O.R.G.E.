#include "builtins.hpp"
#include <cmath>
#include <stdexcept>
namespace forge::detail {
namespace {
template <class T> Json encode(const T& p) {
    if constexpr (std::is_same_v<T, Primitive>)
        return {{"kind", p.kind}};
    else if constexpr (std::is_same_v<T, Tint>)
        return {{"r", p.r}, {"g", p.g}, {"b", p.b}};
    else
        return {{"x", p.x}, {"y", p.y}, {"z", p.z}};
}
template <class T> Value decode(const Json& p) {
    if constexpr (std::is_same_v<T, Primitive>)
        return T{p.at("kind").get<std::uint32_t>()};
    else if constexpr (std::is_same_v<T, Tint>)
        return T{p.at("r"), p.at("g"), p.at("b")};
    else
        return T{p.at("x"), p.at("y"), p.at("z")};
}
template <class T> Json read(flecs::entity e, bool effective) {
    if (effective ? e.has<T>() : e.owns<T>())
        return encode(e.get<T>());
    return nullptr;
}
template <class T> flecs::entity owner(flecs::entity e) { return e.target_for<T>(flecs::IsA); }
template <class T> void apply(flecs::entity e, const std::optional<Value>& value) {
    if (!value) {
        if (e.owns<T>())
            e.remove<T>();
        return;
    }
    const auto& next = std::get<T>(*value);
    if (!e.owns<T>() || e.get<T>() != next)
        e.set<T>(next);
}
template <class T> flecs::entity register_type(flecs::world& w, const char* name) {
    auto c = w.component<T>(name);
    if constexpr (std::is_same_v<T, Position>) {
        ecs_struct_desc_t meta{};
        meta.entity = c.id();
        meta.members[0] = {"x", w.id<float>()};
        meta.members[1] = {"y", w.id<float>()};
        meta.members[2] = {"z", w.id<float>()};
        meta.create_member_entities = true;
        if (!ecs_struct_init(w.c_ptr(), &meta))
            throw std::runtime_error("Position reflection registration failed");
        for (const char* axis : {"x", "y", "z"}) {
            const auto text = std::string("Position along the ") + axis + " axis in world units.";
            c.lookup(axis).set_doc_brief(text.c_str());
        }
    } else if constexpr (std::is_same_v<T, Primitive>)
        c.template member<std::uint32_t>("kind");
    else if constexpr (std::is_same_v<T, Tint>)
        c.template member<float>("r").template member<float>("g").template member<float>("b");
    else
        c.template member<float>("x").template member<float>("y").template member<float>("z");
    c.add(flecs::OnInstantiate, flecs::Inherit);
    return c;
}
template <class T>
Builtin descriptor(const char* name, const char* description, const char* unit,
                   std::optional<double> low, std::optional<double> high,
                   flecs::entity (*reg)(flecs::world&)) {
    return {name, description, unit,    low,      high,    encode(T{}),
            reg,  decode<T>,   read<T>, owner<T>, apply<T>};
}
} // namespace
const std::array<Builtin, 5>& builtins() {
    static const std::array<Builtin, 5> types = {
        descriptor<Position>(
            "forge.position", "Position in world units", "world_units", {}, {},
            [](flecs::world& w) { return register_type<Position>(w, "forge.position"); }),
        descriptor<Rotation>(
            "forge.rotation", "Euler rotation in degrees, X then Y then Z", "degrees", -360000,
            360000, [](flecs::world& w) { return register_type<Rotation>(w, "forge.rotation"); }),
        descriptor<Scale>("forge.scale", "Positive local-axis scale, from 0.001 to 10000",
                          "unitless", 0.001, 10000,
                          [](flecs::world& w) { return register_type<Scale>(w, "forge.scale"); }),
        descriptor<Tint>("forge.tint", "Opaque blockout color, channels from 0 to 1", "unitless", 0,
                         1, [](flecs::world& w) { return register_type<Tint>(w, "forge.tint"); }),
        descriptor<Primitive>(
            "forge.primitive", "Primitive kind: 0 cube, 1 sphere, 2 cylinder, 3 plane", "unitless",
            0, 3, [](flecs::world& w) { return register_type<Primitive>(w, "forge.primitive"); })};
    return types;
}
void validate_components(const Json& components) {
    for (const auto& type : builtins()) {
        if (!components.contains(type.name))
            continue;
        const auto& data = components.at(type.name);
        for (const auto& [field, initial] : type.defaults.items()) {
            const auto& value = data.at(field);
            const bool integral = initial.is_number_unsigned();
            if (!value.is_number() || (integral && !value.is_number_integer()))
                throw std::runtime_error("Component field has invalid numeric type");
            const double n = value.get<double>();
            // Compare float fields in their actual storage precision, preserving v1 bounds.
            const double low = type.minimum
                                   ? (integral ? *type.minimum : double(float(*type.minimum)))
                                   : -double(std::numeric_limits<float>::max());
            const double high =
                type.maximum ? *type.maximum : double(std::numeric_limits<float>::max());
            if (!std::isfinite(n) || (integral ? n : double(value.get<float>())) < low ||
                (integral ? n : double(value.get<float>())) > high)
                throw std::runtime_error("Component field outside supported range");
        }
        (void)type.decode(data);
    }
}
Json register_builtins(flecs::world& world) {
    Json components = Json::array();
    for (const auto& type : builtins()) {
        const auto c = type.register_type(world);
        const auto* structure = ecs_get(world.c_ptr(), c.id(), EcsStruct);
        if (!structure)
            throw std::runtime_error("Reflection metadata is missing");
        Json fields = Json::array();
        const auto* members = ecs_vec_first_t(&structure->members, ecs_member_t);
        for (int i = 0; i < ecs_vec_count(&structure->members); ++i) {
            const auto& m = members[i];
            const bool primitive = m.type == world.id<std::uint32_t>();
            Json f = {{"id", m.name},
                      {"property_id", std::string(type.name) + "." + m.name},
                      {"type", primitive ? "uint32" : "float32"},
                      {"description", type.description},
                      {"default", type.defaults.at(m.name)},
                      {"serialized", true},
                      {"read_only", false},
                      {"animatable", !primitive},
                      {"unit", type.unit}};
            if (type.minimum)
                f["minimum"] = *type.minimum;
            if (type.maximum)
                f["maximum"] = *type.maximum;
            if (primitive)
                f["enum"] = {"Cube", "Sphere", "Cylinder", "Plane"};
            fields.push_back(std::move(f));
        }
        components.push_back({{"id", type.name}, {"schema_version", 1}, {"fields", fields}});
    }
    return {{"version", 1}, {"components", components}};
}
} // namespace forge::detail
