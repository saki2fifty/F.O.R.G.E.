#include "builtins.hpp"
#include <cmath>
#include <stdexcept>
namespace forge::detail {
namespace {
template <class T> Json encode(const T& p) {
    if constexpr (std::is_same_v<T, AudioSource>)
        return {{"clip", p.clip.id ? Json(p.clip.id) : Json()},
                {"play_on_start", p.play_on_start},
                {"loop", p.loop},
                {"gain", p.gain},
                {"pitch", p.pitch},
                {"spatialized", p.spatialized},
                {"minimum_distance", p.minimum_distance},
                {"maximum_distance", p.maximum_distance}};
    else if constexpr (std::is_same_v<T, AudioListener>)
        return {{"enabled", p.enabled}};
    else if constexpr (std::is_same_v<T, PhysicsBody>)
        return {{"motion", p.motion},
                {"density", p.density},
                {"mass", p.mass},
                {"friction", p.friction},
                {"restitution", p.restitution},
                {"gravity_factor", p.gravity_factor}};
    else if constexpr (std::is_same_v<T, SphereCollider>)
        return {{"radius", p.radius}};
    else if constexpr (std::is_same_v<T, CapsuleCollider>)
        return {{"radius", p.radius}, {"height", p.height}};
    else if constexpr (std::is_same_v<T, Primitive>)
        return {{"kind", p.kind}};
    else if constexpr (std::is_same_v<T, Tint>)
        return {{"r", p.r}, {"g", p.g}, {"b", p.b}};
    else if constexpr (std::is_same_v<T, LocalRotation>)
        return {{"x", p.x}, {"y", p.y}, {"z", p.z}, {"w", p.w}};
    else
        return {{"x", p.x}, {"y", p.y}, {"z", p.z}};
}
template <class T> Value decode(const Json& p) {
    if constexpr (std::is_same_v<T, AudioSource>) {
        AudioSource v;
        if (!p.at("clip").is_null())
            v.clip.id = p.at("clip").get<AssetId>();
        v.play_on_start = p.at("play_on_start");
        v.loop = p.at("loop");
        v.gain = p.at("gain");
        v.pitch = p.at("pitch");
        v.spatialized = p.at("spatialized");
        v.minimum_distance = p.at("minimum_distance");
        v.maximum_distance = p.at("maximum_distance");
        if (v.maximum_distance < v.minimum_distance)
            throw std::runtime_error("Audio maximum distance must be at least minimum distance");
        return v;
    } else if constexpr (std::is_same_v<T, AudioListener>)
        return T{p.at("enabled")};
    else if constexpr (std::is_same_v<T, PhysicsBody>)
        return T{p.at("motion").get<std::uint32_t>(),
                 p.at("density"),
                 p.at("mass"),
                 p.at("friction"),
                 p.at("restitution"),
                 p.at("gravity_factor")};
    else if constexpr (std::is_same_v<T, SphereCollider>)
        return T{p.at("radius")};
    else if constexpr (std::is_same_v<T, CapsuleCollider>)
        return T{p.at("radius"), p.at("height")};
    else if constexpr (std::is_same_v<T, Primitive>)
        return T{p.at("kind").get<std::uint32_t>()};
    else if constexpr (std::is_same_v<T, Tint>)
        return T{p.at("r"), p.at("g"), p.at("b")};
    else if constexpr (std::is_same_v<T, LocalRotation>)
        return T{p.at("x"), p.at("y"), p.at("z"), p.at("w")};
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
    if constexpr (std::is_same_v<T, LocalTranslation>) {
        ecs_struct_desc_t meta{};
        meta.entity = c.id();
        meta.members[0] = {"x", w.id<double>()};
        meta.members[1] = {"y", w.id<double>()};
        meta.members[2] = {"z", w.id<double>()};
        meta.create_member_entities = true;
        if (!ecs_struct_init(w.c_ptr(), &meta))
            throw std::runtime_error("LocalTranslation reflection registration failed");
        for (const char* axis : {"x", "y", "z"}) {
            const auto text =
                std::string("LocalTranslation along the ") + axis + " axis in world units.";
            c.lookup(axis).set_doc_brief(text.c_str());
        }
    } else if constexpr (std::is_same_v<T, AudioSource>) {
        w.component<AssetRef<AudioClipAsset>>("forge.audio_clip_ref")
            .opaque(flecs::String)
            .serialize(
                [](const flecs::serializer* serializer, const AssetRef<AudioClipAsset>* value) {
                    const auto text = value->id ? value->id.str() : std::string{};
                    const char* str = text.c_str();
                    return serializer->value(flecs::String, &str);
                });
        c.template member<AssetRef<AudioClipAsset>>("clip")
            .template member<bool>("play_on_start")
            .template member<bool>("loop")
            .template member<float>("gain")
            .template member<float>("pitch")
            .template member<bool>("spatialized")
            .template member<float>("minimum_distance")
            .template member<float>("maximum_distance");
    } else if constexpr (std::is_same_v<T, AudioListener>)
        c.template member<bool>("enabled");
    else if constexpr (std::is_same_v<T, PhysicsBody>)
        c.template member<std::uint32_t>("motion")
            .template member<float>("density")
            .template member<float>("mass")
            .template member<float>("friction")
            .template member<float>("restitution")
            .template member<float>("gravity_factor");
    else if constexpr (std::is_same_v<T, SphereCollider>)
        c.template member<float>("radius");
    else if constexpr (std::is_same_v<T, CapsuleCollider>)
        c.template member<float>("radius").template member<float>("height");
    else if constexpr (std::is_same_v<T, Primitive>)
        c.template member<std::uint32_t>("kind");
    else if constexpr (std::is_same_v<T, Tint>)
        c.template member<float>("r").template member<float>("g").template member<float>("b");
    else if constexpr (std::is_same_v<T, LocalRotation>)
        c.template member<float>("x")
            .template member<float>("y")
            .template member<float>("z")
            .template member<float>("w");
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
const std::array<Builtin, builtin_count>& builtins() {
    static const std::array<Builtin, builtin_count> types = {
        descriptor<LocalTranslation>(
            "forge.local_translation", "Local translation in meters", "meters", {}, {},
            [](flecs::world& w) {
                return register_type<LocalTranslation>(w, "forge.local_translation");
            }),
        descriptor<LocalRotation>(
            "forge.local_rotation", "Normalized local quaternion XYZW", "unitless", -1, 1,
            [](flecs::world& w) {
                return register_type<LocalRotation>(w, "forge.local_rotation");
            }),
        descriptor<LocalScale>(
            "forge.local_scale", "Positive local-axis scale, from 0.001 to 10000", "unitless",
            0.001, 10000,
            [](flecs::world& w) { return register_type<LocalScale>(w, "forge.local_scale"); }),
        descriptor<Tint>("forge.tint", "Opaque blockout color, channels from 0 to 1", "unitless", 0,
                         1, [](flecs::world& w) { return register_type<Tint>(w, "forge.tint"); }),
        descriptor<Primitive>(
            "forge.primitive", "Primitive kind: 0 cube, 1 sphere, 2 cylinder, 3 plane", "unitless",
            0, 3, [](flecs::world& w) { return register_type<Primitive>(w, "forge.primitive"); }),
        descriptor<PhysicsBody>(
            "forge.physics_body", "Body motion: Static, Kinematic, Dynamic. Mass 0 uses density.",
            "unitless", 0, 1000000,
            [](flecs::world& w) { return register_type<PhysicsBody>(w, "forge.physics_body"); }),
        descriptor<BoxCollider>(
            "forge.box_collider", "Centered box full dimensions in meters", "meters", .001, 10000,
            [](flecs::world& w) { return register_type<BoxCollider>(w, "forge.box_collider"); }),
        descriptor<SphereCollider>(
            "forge.sphere_collider", "Centered sphere radius in meters; uniform scale required",
            "meters", .001, 10000,
            [](flecs::world& w) {
                return register_type<SphereCollider>(w, "forge.sphere_collider");
            }),
        descriptor<CapsuleCollider>(
            "forge.capsule_collider",
            "Y-axis capsule; height is straight cylinder height, excluding caps", "meters", .001,
            10000,
            [](flecs::world& w) {
                return register_type<CapsuleCollider>(w, "forge.capsule_collider");
            }),
        descriptor<AudioSource>(
            "forge.audio_source",
            "Gameplay sound source; gain is linear, pitch is a speed multiplier", "unitless", {},
            {},
            [](flecs::world& w) { return register_type<AudioSource>(w, "forge.audio_source"); }),
        descriptor<AudioListener>("forge.audio_listener", "One enabled listener per runtime world",
                                  "unitless", {}, {}, [](flecs::world& w) {
                                      return register_type<AudioListener>(w,
                                                                          "forge.audio_listener");
                                  })};
    return types;
}
Json field_options(const Builtin& type, const std::string& field) {
    Json value = {{"description", type.description}, {"unit", type.unit}};
    if (type.minimum)
        value["minimum"] = *type.minimum;
    if (type.maximum)
        value["maximum"] = *type.maximum;
    const std::string name = type.name;
    if (name == "forge.primitive")
        value["enum"] = {"Cube", "Sphere", "Cylinder", "Plane"};
    if (name == "forge.physics_body") {
        if (field == "motion") {
            value["maximum"] = 2;
            value["enum"] = {"Static", "Kinematic", "Dynamic"};
        }
        if (field == "density") {
            value["minimum"] = .001;
            value["unit"] = "kg/m^3";
        }
        if (field == "mass")
            value["unit"] = "kg (0 uses density)";
        if (field == "friction")
            value["maximum"] = 10;
        if (field == "restitution")
            value["maximum"] = 1;
        if (field == "gravity_factor")
            value["maximum"] = 10;
    }
    if (name == "forge.audio_source") {
        static const std::map<std::string, const char*> help = {
            {"clip", "Registered AudioClip identity; null leaves this source unassigned"},
            {"play_on_start", "Start when this source is first realized; recovered autoplay "
                              "restarts at the beginning"},
            {"loop", "Repeat the clip after reaching its end"},
            {"gain", "Linear gain: zero is silent, one is nominal; values above one amplify"},
            {"pitch",
             "Playback speed and pitch multiplier: one is normal; no independent time stretching"},
            {"spatialized",
             "Use world position and one enabled listener; nonspatial sounds need no listener"},
            {"minimum_distance", "Closest distance used for inverse attenuation"},
            {"maximum_distance",
             "Farthest distance used for inverse attenuation; not a hard mute radius"}};
        value["description"] = help.at(field);
        if (field == "clip") {
            value["asset_type"] = AudioClipAsset::type;
            value["nullable"] = true;
        }
        if (field == "gain") {
            value["minimum"] = 0;
            value["maximum"] = 4;
        }
        if (field == "pitch") {
            value["minimum"] = .25;
            value["maximum"] = 4;
        }
        if (field.ends_with("distance")) {
            value["minimum"] = .001;
            value["maximum"] = 10000;
            value["unit"] = "meters";
        }
    }
    return value;
}
void validate_components(const Json& components) {
    for (const auto& type : builtins()) {
        if (!components.contains(type.name))
            continue;
        const auto& data = components.at(type.name);
        for (const auto& [field, initial] : type.defaults.items()) {
            const auto& value = data.at(field);
            if (initial.is_null()) {
                if (!value.is_null())
                    (void)value.get<AssetId>();
                continue;
            }
            if (initial.is_boolean()) {
                if (!value.is_boolean())
                    throw std::runtime_error("Component field requires a boolean");
                continue;
            }
            const bool integral = initial.is_number_unsigned();
            const bool wide = std::string(type.name) == "forge.local_translation";
            if (!value.is_number() || (integral && !value.is_number_integer()))
                throw std::runtime_error("Component field has invalid numeric type");
            const double n = value.get<double>();
            // Compare float fields in their actual storage precision, preserving v1 bounds.
            const auto options = field_options(type, field);
            const std::optional<double> minimum = options.contains("minimum")
                                                      ? std::optional<double>(options.at("minimum"))
                                                      : std::nullopt;
            const std::optional<double> maximum = options.contains("maximum")
                                                      ? std::optional<double>(options.at("maximum"))
                                                      : std::nullopt;
            const double low = minimum ? (integral ? *minimum : double(float(*minimum)))
                                       : -(wide ? std::numeric_limits<double>::max()
                                                : double(std::numeric_limits<float>::max()));
            const double high = maximum ? *maximum
                                        : (wide ? std::numeric_limits<double>::max()
                                                : double(std::numeric_limits<float>::max()));
            if (!std::isfinite(n) || ((integral || wide) ? n : double(value.get<float>())) < low ||
                ((integral || wide) ? n : double(value.get<float>())) > high)
                throw std::runtime_error("Component field outside supported range");
        }
        (void)type.decode(data);
    }
}
Json register_builtins(flecs::world& world, unsigned family) {
    Json components = Json::array();
    for (const auto& type : builtins()) {
        const std::string name = type.name;
        const unsigned category = name.starts_with("forge.audio_") ? 2u
                                  : (name == "forge.physics_body" || name.ends_with("_collider"))
                                      ? 1u
                                      : 0u;
        if (category != family)
            continue;
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
                      {"type", type.defaults.at(m.name).is_null()      ? "asset_ref"
                               : type.defaults.at(m.name).is_boolean() ? "bool"
                               : primitive
                                   ? "uint32"
                                   : (m.type == world.id<double>() ? "float64" : "float32")},
                      {"description", type.description},
                      {"default", type.defaults.at(m.name)},
                      {"serialized", true},
                      {"read_only", false},
                      {"animatable", !primitive && type.defaults.at(m.name).is_number()},
                      {"unit", type.unit}};
            f.update(field_options(type, m.name));
            fields.push_back(std::move(f));
        }
        components.push_back({{"id", type.name},
                              {"schema_version", 1},
                              {"fields", fields},
                              {"optional", category != 0}});
    }
    return {{"version", 1}, {"components", components}};
}
} // namespace forge::detail
