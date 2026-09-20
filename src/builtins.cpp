#include "builtins.hpp"
#include "reflected_value.hpp"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <forge/primitive_catalog.hpp>
#include <mutex>
#include <stdexcept>
namespace forge::detail {
namespace {
template <class T> Json encode(const T& p) {
    if constexpr (std::is_same_v<T, UiDocument>)
        return {{"document", p.document.id ? Json(p.document.id) : Json()},
                {"enabled", p.enabled},
                {"visible", p.visible},
                {"layer", p.layer}};
    else if constexpr (std::is_same_v<T, NavigationSurface>)
        return {{"enabled", p.enabled}};
    else if constexpr (std::is_same_v<T, NavigationAgent>)
        return {{"navmesh", p.navmesh.id ? Json(p.navmesh.id) : Json()},
                {"enabled", p.enabled},
                {"has_destination", p.has_destination},
                {"speed", p.speed},
                {"stopping_distance", p.stopping_distance},
                {"destination_x", p.destination_x},
                {"destination_y", p.destination_y},
                {"destination_z", p.destination_z}};
    else if constexpr (std::is_same_v<T, Animator>)
        return {{"skeleton", p.skeleton.id ? Json(p.skeleton.id) : Json()},
                {"clip", p.clip.id ? Json(p.clip.id) : Json()},
                {"enabled", p.enabled},
                {"play_on_start", p.play_on_start},
                {"loop", p.loop},
                {"playback_speed", p.playback_speed}};
    else if constexpr (std::is_same_v<T, AudioSource>)
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
    else if constexpr (std::is_same_v<T, LocalScale>)
        return {{"x", p.x == 0 ? 0.0f : p.x},
                {"y", p.y == 0 ? 0.0f : p.y},
                {"z", p.z == 0 ? 0.0f : p.z}};
    else if constexpr (std::is_same_v<T, LocalRotation>)
        return {{"x", p.x}, {"y", p.y}, {"z", p.z}, {"w", p.w}};
    else
        return {{"x", p.x}, {"y", p.y}, {"z", p.z}};
}
template <class T> Value decode(const Json& p) {
    if constexpr (std::is_same_v<T, LocalScale>) {
        return checked_local_scale({p.at("x"), p.at("y"), p.at("z")});
    } else if constexpr (std::is_same_v<T, UiDocument>) {
        UiDocument v;
        if (!p.at("document").is_null())
            v.document.id = p.at("document").get<AssetId>();
        v.enabled = p.at("enabled");
        v.visible = p.at("visible");
        v.layer = p.at("layer");
        return v;
    } else if constexpr (std::is_same_v<T, NavigationSurface>)
        return T{p.at("enabled")};
    else if constexpr (std::is_same_v<T, NavigationAgent>) {
        NavigationAgent a;
        if (!p.at("navmesh").is_null())
            a.navmesh.id = p.at("navmesh").get<AssetId>();
        a.enabled = p.at("enabled");
        a.has_destination = p.at("has_destination");
        a.speed = p.at("speed");
        a.stopping_distance = p.at("stopping_distance");
        a.destination_x = p.at("destination_x");
        a.destination_y = p.at("destination_y");
        a.destination_z = p.at("destination_z");
        return a;
    } else if constexpr (std::is_same_v<T, Animator>) {
        Animator v;
        if (!p.at("skeleton").is_null())
            v.skeleton.id = p.at("skeleton").get<AssetId>();
        if (!p.at("clip").is_null())
            v.clip.id = p.at("clip").get<AssetId>();
        v.enabled = p.at("enabled");
        v.play_on_start = p.at("play_on_start");
        v.loop = p.at("loop");
        v.playback_speed = p.at("playback_speed");
        return v;
    } else if constexpr (std::is_same_v<T, AudioSource>) {
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
template <class T> void register_asset_ref(flecs::world& w, const char* name) {
    w.component<AssetRef<T>>(name)
        .opaque(flecs::String)
        .serialize([](const flecs::serializer* serializer, const AssetRef<T>* ref) {
            const auto text = ref->id ? ref->id.str() : std::string{};
            const char* str = text.c_str();
            return serializer->value(flecs::String, &str);
        });
}
template <class T> flecs::entity register_type(flecs::world& w, const char* name) {
    auto c = w.component<T>(name);
    if constexpr (std::is_same_v<T, LocalTranslation>) {
        c.template member<double>("x").template member<double>("y").template member<double>("z");
    } else if constexpr (std::is_same_v<T, UiDocument>) {
        register_asset_ref<UiDocumentAsset>(w, "forge.ui_document_ref");
        c.template member<AssetRef<UiDocumentAsset>>("document")
            .template member<bool>("enabled")
            .template member<bool>("visible")
            .template member<std::uint32_t>("layer");
    } else if constexpr (std::is_same_v<T, NavigationSurface>)
        c.template member<bool>("enabled");
    else if constexpr (std::is_same_v<T, NavigationAgent>) {
        register_asset_ref<NavMeshAsset>(w, "forge.navmesh_ref");
        c.template member<AssetRef<NavMeshAsset>>("navmesh")
            .template member<bool>("enabled")
            .template member<bool>("has_destination")
            .template member<float>("speed")
            .template member<float>("stopping_distance")
            .template member<double>("destination_x")
            .template member<double>("destination_y")
            .template member<double>("destination_z");
    } else if constexpr (std::is_same_v<T, Animator>) {
        register_asset_ref<SkeletonAsset>(w, "forge.skeleton_ref");
        register_asset_ref<AnimationClipAsset>(w, "forge.animation_clip_ref");
        c.template member<AssetRef<SkeletonAsset>>("skeleton")
            .template member<AssetRef<AnimationClipAsset>>("clip")
            .template member<bool>("enabled")
            .template member<bool>("play_on_start")
            .template member<bool>("loop")
            .template member<float>("playback_speed");
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
            "forge.local_scale",
            "Signed visual local-axis scale, from -10000 to +10000; zero is valid", "unitless",
            -10000, 10000,
            [](flecs::world& w) { return register_type<LocalScale>(w, "forge.local_scale"); }),
        descriptor<Tint>("forge.tint", "Opaque blockout color, channels from 0 to 1", "unitless", 0,
                         1, [](flecs::world& w) { return register_type<Tint>(w, "forge.tint"); }),
        descriptor<Primitive>(
            "forge.primitive", "Built-in blockout geometry; None disables geometry", "unitless", 0,
            primitive_count - 1,
            [](flecs::world& w) { return register_type<Primitive>(w, "forge.primitive"); }),
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
        descriptor<AudioListener>(
            "forge.audio_listener", "One enabled listener per runtime world", "unitless", {}, {},
            [](flecs::world& w) {
                return register_type<AudioListener>(w, "forge.audio_listener");
            }),
        descriptor<Animator>(
            "forge.animator", "Single-clip skeletal animation; derived poses only", "unitless", {},
            {}, [](flecs::world& w) { return register_type<Animator>(w, "forge.animator"); }),
        descriptor<NavigationSurface>(
            "forge.navigation_surface",
            "Include static primitive geometry when building navigation", "unitless", {}, {},
            [](flecs::world& w) {
                return register_type<NavigationSurface>(w, "forge.navigation_surface");
            }),
        descriptor<NavigationAgent>(
            "forge.navigation_agent", "Fixed-tick path following for nonphysics entities",
            "unitless", {}, {},
            [](flecs::world& w) {
                return register_type<NavigationAgent>(w, "forge.navigation_agent");
            }),
        descriptor<UiDocument>(
            "forge.ui_document", "Runtime UI document displayed during Play", "unitless", {}, {},
            [](flecs::world& w) { return register_type<UiDocument>(w, "forge.ui_document"); })};
    return types;
}
Json registration_options(const Builtin& type, const std::string& field) {
    Json value = {{"description", type.description}, {"unit", type.unit}};
    if (type.minimum)
        value["minimum"] = *type.minimum;
    if (type.maximum)
        value["maximum"] = *type.maximum;
    const std::string name = type.name;
    if (name == "forge.physics_body") {
        if (field == "motion") {
            value["maximum"] = 2;
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
    if (name == "forge.animator") {
        if (field == "skeleton" || field == "clip") {
            value["asset_type"] =
                field == "skeleton" ? SkeletonAsset::type : AnimationClipAsset::type;
            value["nullable"] = true;
            value["description"] =
                field == "skeleton"
                    ? "Registered skeleton identity; clip must bind to this exact skeleton revision"
                    : "Registered clip identity produced for the chosen skeleton";
        } else if (field == "playback_speed") {
            value["minimum"] = 0;
            value["maximum"] = 4;
            value["description"] =
                "Fixed-tick playback speed multiplier; zero holds the pose, one is normal";
        } else
            value["description"] = field == "enabled" ? "Evaluate this Animator during Play"
                                   : field == "loop"
                                       ? "Repeat the clip at its duration"
                                       : "Begin playback when this Animator is realized";
    }
    if (name == "forge.ui_document") {
        if (field == "document") {
            value["asset_type"] = UiDocumentAsset::type;
            value["nullable"] = true;
            value["description"] = "Registered RML document identity";
        } else if (field == "layer") {
            value["minimum"] = 0;
            value["maximum"] = 255;
            value["description"] = "Higher layers draw above lower layers";
        } else
            value["description"] = field == "enabled" ? "Load this document during Play"
                                                      : "Show this document during Play";
    }
    if (name == "forge.navigation_agent") {
        if (field == "navmesh") {
            value["asset_type"] = NavMeshAsset::type;
            value["nullable"] = true;
            value["description"] =
                "Baked navigation asset for this scene; rebuild after changing included geometry";
        }
        if (field == "speed") {
            value["minimum"] = 0;
            value["maximum"] = 20;
            value["unit"] = "meters/second";
            value["description"] = "Fixed-tick movement speed; zero holds position";
        }
        if (field == "stopping_distance") {
            value["minimum"] = .01;
            value["maximum"] = 5;
            value["unit"] = "meters";
            value["description"] = "Stop within this distance of the projected destination";
        }
        if (field.starts_with("destination_")) {
            value["minimum"] = -4090;
            value["maximum"] = 4090;
            value["unit"] = "meters";
            value["description"] = "Destination in world coordinates, +Y up";
        }
        if (field == "enabled")
            value["description"] = "Enable nonphysics movement during Play; no crowd avoidance";
        if (field == "has_destination")
            value["description"] =
                "Request a route to the destination; partial or invalid paths do not move";
    }
    return value;
}
namespace {
std::string friendly_name(std::string text) {
    bool initial = true;
    for (auto& ch : text) {
        if (ch == '_') {
            ch = ' ';
            initial = true;
        } else if (initial) {
            ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
            initial = false;
        }
    }
    return text;
}
// This is a detached, immutable projection of registered metadata, never entity state.
// Ordinary commands do not construct validation worlds. Standalone file validators
// can initialize the same catalog before any EngineContext exists.
std::recursive_mutex catalog_mutex;
std::map<std::string, Json> validation_catalog;
ecs_entity_t field_unit(flecs::world& world, const std::string& name) {
    if (name == "meters")
        return EcsMeters;
    if (name == "meters/second")
        return EcsMetersPerSecond;
    if (name == "kg (0 uses density)")
        return EcsKiloGrams;
    if (name == "kg/m^3") {
        auto unit = world.lookup("forge.units.KilogramsPerCubicMeter");
        if (!unit) {
            ecs_unit_desc_t desc{};
            desc.entity = world.entity("forge.units.KilogramsPerCubicMeter").id();
            desc.symbol = "kg/m^3";
            if (!ecs_unit_init(world.c_ptr(), &desc))
                throw std::runtime_error("Density unit registration failed");
            unit = world.entity(desc.entity);
        }
        return unit.id();
    }
    return 0; // Quaternions, scale and multipliers are dimensionless, not angles.
}
ecs_entity_t enum_type(flecs::world& world, bool primitive) {
    const char* name = primitive ? "forge.PrimitiveKind" : "forge.PhysicsMotion";
    auto type = world.lookup(name);
    if (type)
        return type.id();
    const char* motions[] = {"Static", "Kinematic", "Dynamic"};
    const unsigned count = primitive ? primitive_count : 3;
    std::vector<std::string> names;
    for (unsigned i = 0; i < count; ++i) {
        names.emplace_back(primitive ? primitive_names[i] : motions[i]);
        std::replace(names.back().begin(), names.back().end(), ' ', '_');
    }
    ecs_enum_desc_t desc{};
    desc.entity = world.entity(name);
    desc.underlying_type = world.id<std::uint32_t>();
    for (unsigned i = 0; i < count; ++i) {
        desc.constants[i].name = names[i].c_str();
        desc.constants[i].value_unsigned = i;
    }
    if (!ecs_enum_init(world, &desc))
        throw std::runtime_error("Native enum registration failed");
    type = world.entity(desc.entity);
    for (unsigned i = 0; i < count; ++i)
        type.lookup(names[i].c_str()).set_doc_name(primitive ? primitive_names[i] : motions[i]);
    return type;
}
void annotate_type(flecs::world& world, flecs::entity component, const Builtin& type) {
    const auto* structure = ecs_get(world.c_ptr(), component.id(), EcsStruct);
    if (!structure)
        throw std::runtime_error("Reflection metadata is missing");
    const int count = ecs_vec_count(&structure->members);
    if (count > ECS_MEMBER_DESC_CACHE_SIZE)
        throw std::runtime_error("Builtin exceeds reflection descriptor capacity");
    // Copy names and descriptors before registration can reallocate EcsStruct storage.
    std::vector<std::string> names;
    std::vector<ecs_member_t> members;
    const auto* source = ecs_vec_first_t(&structure->members, ecs_member_t);
    for (int i = 0; i < count; ++i) {
        names.emplace_back(source[i].name);
        members.push_back(source[i]);
    }
    ecs_struct_desc_t desc{};
    desc.entity = component.id();
    desc.create_member_entities = true;
    for (int i = 0; i < count; ++i) {
        auto& m = desc.members[i];
        m = members[i];
        m.name = names[i].c_str();
        m.use_offset = true;
        const auto options = registration_options(type, m.name);
        m.unit = field_unit(world, options.at("unit"));
        const bool enumeration =
            std::string(type.name) == "forge.primitive" ||
            (std::string(type.name) == "forge.physics_body" && names[i] == "motion");
        if (enumeration)
            m.type = enum_type(world, std::string(type.name) == "forge.primitive");
        // Stable Meta ranges require primitive numbers, not enums. Native enum
        // constants define membership; no parallel numeric range is registered.
        if (!enumeration && options.contains("minimum") && options.contains("maximum")) {
            m.range = {options.at("minimum").get<double>(), options.at("maximum").get<double>()};
            m.error_range = m.range;
        }
        // Above nominal gain is valid amplification, but warrants contextual guidance.
        if (std::string(type.name) == "forge.audio_source" && names[i] == "gain")
            m.warning_range = {0, 1};
    }
    // Flecs 4.1.6 member-entity helpers lose explicit zero-offset intent.
    // Physical order avoids a later zero offset recomputing earlier members.
    // Keep names/field semantics independent of registration order, and verify
    // the native result before a codec can use it (see flecs-known-issues.md).
    std::stable_sort(desc.members, desc.members + count,
                     [](const auto& a, const auto& b) { return a.offset < b.offset; });
    if (!ecs_struct_init(world.c_ptr(), &desc))
        throw std::runtime_error("Builtin member metadata registration failed");
    for (int i = 0; i < count; ++i) {
        const auto* actual = ecs_struct_get_member(world.c_ptr(), component.id(), names[i].c_str());
        if (!actual || actual->offset != members[i].offset || actual->size != members[i].size)
            throw std::runtime_error("Native member metadata changed the typed component layout");
    }
    const std::string id = type.name;
    const auto display =
        id == "forge.ui_document" ? "UI Document" : friendly_name(id.substr(id.find('.') + 1));
    component.set_doc_name(display.c_str()).set_doc_brief(type.description);
    for (const auto& name : names) {
        auto member = component.lookup(name.c_str());
        const auto options = registration_options(type, name);
        auto help = options.at("description").get<std::string>();
        if (id == "forge.local_translation")
            help = "Local translation along the " + name + " axis, in meters.";
        if (id == "forge.physics_body" && name == "mass")
            help = "Mass in kilograms; zero computes mass from density and collider volume.";
        member.set_doc_name(friendly_name(name).c_str()).set_doc_brief(help.c_str());
    }
}
Json validation_schema(const char* name) {
    std::lock_guard lock(catalog_mutex);
    if (!validation_catalog.contains(name)) {
        // Standalone schema/file tools have no gameplay world to borrow. This
        // short-lived metadata-only world contains no scene, modules or systems.
        flecs::world metadata;
        for (unsigned family = 0; family != 6; ++family)
            register_builtins(metadata, family);
    }
    return validation_catalog.at(name);
}
} // namespace
Json register_builtins(flecs::world& world, unsigned family) {
    world.import<flecs::units>();
    Json components = Json::array();
    for (const auto& type : builtins()) {
        const std::string name = type.name;
        const unsigned category = name == "forge.ui_document"             ? 5u
                                  : name.starts_with("forge.navigation_") ? 4u
                                  : name == "forge.animator"              ? 3u
                                  : name.starts_with("forge.audio_")      ? 2u
                                  : (name == "forge.physics_body" || name.ends_with("_collider"))
                                      ? 1u
                                      : 0u;
        if (category != family)
            continue;
        const auto c = type.register_type(world);
        annotate_type(world, c, type);
        std::vector<ReflectedReference> references;
        // Only explicitly registered FORGE references cross the opaque boundary.
        for (const auto& [path, asset] :
             {std::pair{"forge.audio_clip_ref", AudioClipAsset::type},
              std::pair{"forge.skeleton_ref", SkeletonAsset::type},
              std::pair{"forge.animation_clip_ref", AnimationClipAsset::type},
              std::pair{"forge.navmesh_ref", NavMeshAsset::type},
              std::pair{"forge.ui_document_ref", UiDocumentAsset::type}}) {
            if (auto ref = world.lookup(path))
                references.push_back({ref.id(), "asset_ref", asset});
        }
        auto fields = reflected_type_schema(world, c.id(), references).at("fields");
        for (auto& field : fields) {
            const auto member = field.at("id").get<std::string>();
            field["property_id"] = name + "." + member;
            field["default"] = type.defaults.at(member);
        }
        const char* categories[] = {
            "Rendering / Transform", "Physics", "Audio", "Animation", "Navigation", "Runtime UI"};
        components.push_back({{"id", type.name},
                              {"display_name", ecs_doc_get_name(world.c_ptr(), c.id())},
                              {"description", ecs_doc_get_brief(world.c_ptr(), c.id())},
                              {"category", categories[category]},
                              {"schema_version", 1},
                              {"fields", fields},
                              {"optional", category != 0}});
        std::lock_guard lock(catalog_mutex);
        validation_catalog.try_emplace(type.name, components.back());
    }
    return {{"version", 1}, {"components", components}};
}
void validate_reflected_value(flecs::world world, ecs_entity_t type, const void* value) {
    const auto* structure = ecs_get(world.c_ptr(), type, EcsStruct);
    if (!structure || !value)
        throw std::runtime_error("Missing component validation metadata/value");
    const auto* members = ecs_vec_first_t(&structure->members, ecs_member_t);
    for (int i = 0; i < ecs_vec_count(&structure->members); ++i) {
        const auto& m = members[i];
        const auto* enumeration = ecs_get(world.c_ptr(), m.type, EcsEnum);
        const auto* primitive = ecs_get(
            world.c_ptr(), enumeration ? enumeration->underlying_type : m.type, EcsPrimitive);
        if (!primitive || primitive->kind == EcsBool)
            continue;
        const auto* ptr = static_cast<const std::byte*>(value) + m.offset;
        double number = 0;
        bool single = false;
        switch (primitive->kind) {
        case EcsF32: {
            float v;
            std::memcpy(&v, ptr, sizeof(v));
            number = v;
            single = true;
            break;
        }
        case EcsF64:
            std::memcpy(&number, ptr, sizeof(number));
            break;
        case EcsU32: {
            std::uint32_t v;
            std::memcpy(&v, ptr, sizeof(v));
            number = v;
            break;
        }
        default:
            throw std::runtime_error("Unsupported builtin validation storage");
        }
        if (enumeration) {
            const auto* constants = ecs_get(world.c_ptr(), m.type, EcsConstants);
            const auto* values =
                ecs_vec_first_t(&constants->ordered_constants, ecs_enum_constant_t);
            bool found = false;
            for (int j = 0; j < ecs_vec_count(&constants->ordered_constants); ++j)
                found |= values[j].value_unsigned == static_cast<std::uint64_t>(number);
            if (!found)
                throw std::runtime_error(std::string(m.name) + ": unsupported enum value");
        }
        const double low = single ? double(float(m.range.min)) : m.range.min;
        if (!std::isfinite(number) ||
            (m.range.min != m.range.max && (number < low || number > m.range.max)))
            throw std::runtime_error(std::string(ecs_get_name(world.c_ptr(), type)) + "." + m.name +
                                     ": component field outside supported range");
    }
}
void validate_components(const Json& components) {
    for (const auto& type : builtins()) {
        if (!components.contains(type.name))
            continue;
        const auto& data = components.at(type.name);
        const auto schema = validation_schema(type.name);
        // Preserve the legacy opaque extension envelope. The bounded reflected
        // payload contains only this builtin's known fields, never unknown data.
        Json known = Json::object();
        for (const auto& field : schema.at("fields")) {
            const auto key = field.at("id").get<std::string>();
            known[key] = data.at(key);
        }
        validate_reflected_json(
            {{"type", "struct"}, {"id", type.name}, {"fields", schema.at("fields")}}, known);
        (void)type.decode(data); // Cross-member invariants remain FORGE-owned.
    }
}
} // namespace forge::detail
