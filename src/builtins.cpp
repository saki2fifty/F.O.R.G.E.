#include "builtins.hpp"
#include "authored_schema.hpp"
#include "material_parameter_overrides.hpp"
#include "material_slot.hpp"
#include "physics_joint_value.hpp"
#include "reflected_extensions.hpp"
#include "reflected_string.hpp"
#include "reflected_value.hpp"
#include "reflected_vector.hpp"
#include "render_values.hpp"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <forge/primitive_catalog.hpp>
#include <forge/render_view.hpp>
#include <mutex>
#include <numbers>
#include <stdexcept>
namespace forge::detail {
namespace {
template <class T> Json encode(const T& p) {
    if constexpr (std::is_same_v<T, NodeVisibility>)
        return {{"visible", p.visible}};
    else if constexpr (std::is_same_v<T, NodeSelectability>)
        return {{"selectable", p.selectable}};
    else if constexpr (std::is_same_v<T, Camera>)
        return render_value(p);
    else if constexpr (std::is_same_v<T, Light>)
        return render_value(p);
    else if constexpr (std::is_same_v<T, SpriteRenderer>)
        return render_value(p);
    else if constexpr (std::is_same_v<T, ModelSource>) {
        return {{"model", p.model.id ? Json(p.model.id) : Json()},
                {"node", p.node.id ? Json(p.node.id) : Json()}};
    } else if constexpr (std::is_same_v<T, MeshRenderer>) {
        Json slots = Json::array();
        for (const auto& slot : p.materials)
            slots.push_back({{"slot", slot.slot},
                             {"material", slot.material.id ? Json(slot.material.id) : Json()}});
        validate_material_parameter_overrides(p.material_parameters);
        Json parameters = Json::array();
        for (const auto& entry : p.material_parameters)
            parameters.push_back(
                {{"slot", entry.slot},
                 {"key", entry.key},
                 {"parameter",
                  {{"type", unsigned(entry.parameter.type)}, {"value", entry.parameter.value}}}});
        return {{"mesh", p.mesh.id ? Json(p.mesh.id) : Json()},
                {"materials", slots},
                {"material_parameters", parameters},
                {"material_variant", p.material_variant.id ? Json(p.material_variant.id) : Json()},
                {"enabled", p.enabled},
                {"visible", p.visible},
                {"cast_shadows", p.cast_shadows},
                {"receive_shadows", p.receive_shadows},
                {"layers", p.layers}};
    } else if constexpr (std::is_same_v<T, ParticleEmitter>)
        return render_value(p);
    else if constexpr (std::is_same_v<T, UiDocument>)
        return {{"document", p.document.id ? Json(p.document.id) : Json()},
                {"enabled", p.enabled},
                {"visible", p.visible},
                {"layer", p.layer}};
    else if constexpr (std::is_same_v<T, NavigationSurface>)
        return {{"enabled", p.enabled}};
    else if constexpr (std::is_same_v<T, NavigationAvoidance>)
        return {{"enabled", p.enabled}, {"horizon", p.horizon}};
    else if constexpr (std::is_same_v<T, NavigationAgent>)
        return {{"navmesh", p.navmesh.id ? Json(p.navmesh.id) : Json()},
                {"enabled", p.enabled},
                {"has_destination", p.has_destination},
                {"speed", p.speed},
                {"stopping_distance", p.stopping_distance},
                {"destination_x", p.destination_x},
                {"destination_y", p.destination_y},
                {"destination_z", p.destination_z}};
    else if constexpr (std::is_same_v<T, AnimationSequence>)
        return {{"duration", p.duration},
                {"enabled", p.enabled},
                {"play_on_start", p.play_on_start},
                {"loop", p.loop},
                {"playback_speed", p.playback_speed}};
    else if constexpr (std::is_same_v<T, CameraShot>)
        return {{"sequence", p.sequence.entity ? Json(p.sequence) : Json()},
                {"camera", p.camera.entity ? Json(p.camera) : Json()},
                {"start_seconds", p.start_seconds},
                {"end_seconds", p.end_seconds},
                {"enabled", p.enabled}};
    else if constexpr (std::is_same_v<T, AudioCue>)
        return {{"sequence", p.sequence.entity ? Json(p.sequence) : Json()},
                {"source", p.source.entity ? Json(p.source) : Json()},
                {"time_seconds", p.time_seconds},
                {"enabled", p.enabled}};
    else if constexpr (std::is_same_v<T, TransformAnimator>)
        return {{"clip", p.clip.id ? Json(p.clip.id) : Json()},
                {"enabled", p.enabled},
                {"play_on_start", p.play_on_start},
                {"loop", p.loop},
                {"playback_speed", p.playback_speed},
                {"sequence", p.sequence.entity ? Json(p.sequence) : Json()},
                {"sequence_start_seconds", p.sequence_start_seconds}};
    else if constexpr (std::is_same_v<T, TwoBoneIK> || std::is_same_v<T, AimIK>) {
        Json value = {
            {"enabled", p.enabled},   {"weight", p.weight},     {"twist_degrees", p.twist_degrees},
            {"target_x", p.target_x}, {"target_y", p.target_y}, {"target_z", p.target_z},
            {"pole_x", p.pole_x},     {"pole_y", p.pole_y},     {"pole_z", p.pole_z}};
        const auto ref = [](auto value) { return value.id ? Json(value.id) : Json(); };
        if constexpr (std::is_same_v<T, TwoBoneIK>) {
            value["start"] = ref(p.start);
            value["middle"] = ref(p.middle);
            value["end"] = ref(p.end);
            value["soften"] = p.soften;
            value["axis_x"] = p.axis_x;
            value["axis_y"] = p.axis_y;
            value["axis_z"] = p.axis_z;
        } else {
            value["joint"] = ref(p.joint);
            value["forward_x"] = p.forward_x;
            value["forward_y"] = p.forward_y;
            value["forward_z"] = p.forward_z;
            value["up_x"] = p.up_x;
            value["up_y"] = p.up_y;
            value["up_z"] = p.up_z;
        }
        return value;
    } else if constexpr (std::is_same_v<T, Animator>)
        return {{"skeleton", p.skeleton.id ? Json(p.skeleton.id) : Json()},
                {"clip", p.clip.id ? Json(p.clip.id) : Json()},
                {"enabled", p.enabled},
                {"play_on_start", p.play_on_start},
                {"loop", p.loop},
                {"playback_speed", p.playback_speed},
                {"blend_clip", p.blend_clip.id ? Json(p.blend_clip.id) : Json()},
                {"blend_weight", p.blend_weight},
                {"transition_seconds", p.transition_seconds},
                {"states", p.states.id ? Json(p.states.id) : Json()},
                {"state", p.state},
                {"root_motion", p.root_motion},
                {"root_joint", p.root_joint.id ? Json(p.root_joint.id) : Json()},
                {"blend_root", p.blend_root.id ? Json(p.blend_root.id) : Json()},
                {"blend_mode", p.blend_mode},
                {"blend_mask", p.blend_mask.id ? Json(p.blend_mask.id) : Json()},
                {"blend_space", p.blend_space.id ? Json(p.blend_space.id) : Json()},
                {"blend_position", p.blend_position},
                {"blend_position_y", p.blend_position_y}};
    else if constexpr (std::is_same_v<T, AudioSource>)
        return {{"clip", p.clip.id ? Json(p.clip.id) : Json()},
                {"play_on_start", p.play_on_start},
                {"loop", p.loop},
                {"gain", p.gain},
                {"pitch", p.pitch},
                {"spatialized", p.spatialized},
                {"minimum_distance", p.minimum_distance},
                {"maximum_distance", p.maximum_distance},
                {"attenuation", p.attenuation},
                {"rolloff", p.rolloff},
                {"cone_enabled", p.cone_enabled},
                {"inner_angle", p.inner_angle},
                {"outer_angle", p.outer_angle},
                {"outer_gain", p.outer_gain},
                {"bus", p.bus},
                {"doppler", p.doppler}};
    else if constexpr (std::is_same_v<T, AudioListener>)
        return {{"enabled", p.enabled}};
    else if constexpr (std::is_same_v<T, CharacterController>)
        return {{"enabled", p.enabled},
                {"shape", p.shape},
                {"radius", p.radius},
                {"height", p.height},
                {"crouch_height", p.crouch_height},
                {"mass", p.mass},
                {"max_strength", p.max_strength},
                {"max_slope", p.max_slope},
                {"step_height", p.step_height},
                {"step_forward", p.step_forward},
                {"floor_probe", p.floor_probe},
                {"gravity_factor", p.gravity_factor},
                {"layer", p.layer},
                {"mask", p.mask}};
    else if constexpr (std::is_same_v<T, PhysicsBody>)
        return {{"motion", p.motion},
                {"density", p.density},
                {"mass", p.mass},
                {"friction", p.friction},
                {"restitution", p.restitution},
                {"gravity_factor", p.gravity_factor},
                {"enabled", p.enabled},
                {"sensor", p.sensor},
                {"layer", p.layer},
                {"mask", p.mask},
                {"continuous_collision", p.continuous_collision},
                {"linear_damping", p.linear_damping},
                {"angular_damping", p.angular_damping},
                {"max_linear_velocity", p.max_linear_velocity},
                {"max_angular_velocity", p.max_angular_velocity},
                {"allow_sleeping", p.allow_sleeping}};
    else if constexpr (std::is_same_v<T, PhysicsJoint>)
        return physics_detail::joint_value(p);
    else if constexpr (std::is_same_v<T, AssetCollider>)
        return {{"asset", p.asset.id ? Json(p.asset.id) : Json()}};
    else if constexpr (std::is_same_v<T, SphereCollider>)
        return {{"radius", p.radius}};
    else if constexpr (std::is_same_v<T, CapsuleCollider> || std::is_same_v<T, CylinderCollider>)
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
    if constexpr (std::is_same_v<T, NodeVisibility>)
        return T{p.at("visible").get<bool>()};
    else if constexpr (std::is_same_v<T, NodeSelectability>)
        return T{p.at("selectable").get<bool>()};
    else if constexpr (std::is_same_v<T, Camera>)
        return camera_value(p);
    else if constexpr (std::is_same_v<T, Light>)
        return light_value(p);
    else if constexpr (std::is_same_v<T, ModelSource>) {
        ModelSource value;
        if (!p.at("model").is_null())
            value.model.id = p.at("model").get<AssetId>();
        if (!p.at("node").is_null())
            value.node.id = p.at("node").get<AssetId>();
        if (value.node.id && !value.model.id)
            throw std::runtime_error("Model node provenance requires its source Model");
        return value;
    } else if constexpr (std::is_same_v<T, MeshRenderer>) {
        MeshRenderer value;
        if (!p.at("mesh").is_null())
            value.mesh.id = p.at("mesh").get<AssetId>();
        for (const auto& slot : p.at("materials")) {
            MaterialSlotOverride entry{slot.at("slot").get<std::string>(), {}};
            if (!slot.at("material").is_null())
                entry.material.id = slot.at("material").get<AssetId>();
            value.materials.push_back(std::move(entry));
        }
        validate_material_slots(value.materials);
        if (p.contains("material_parameters")) {
            const auto& parameters = p.at("material_parameters");
            if (!parameters.is_array() || parameters.size() > material_parameter_override_limit)
                throw std::runtime_error("Object material parameter count exceeds64");
            value.material_parameters.reserve(parameters.size());
            for (const auto& entry : parameters) {
                const auto& parameter = entry.at("parameter");
                const auto& type = parameter.at("type");
                if (!type.is_number_integer() ||
                    type.get<std::uint64_t>() > unsigned(MaterialParameterType::LinearColor4))
                    throw std::runtime_error("Invalid object material parameter type");
                const auto& lanes = parameter.at("value");
                if (!lanes.is_array() || lanes.size() != 4)
                    throw std::runtime_error("Object material values require four lanes");
                value.material_parameters.push_back(
                    {entry.at("slot").get<std::string>(),
                     entry.at("key").get<std::string>(),
                     {static_cast<MaterialParameterType>(type.get<unsigned>()),
                      lanes.get<std::array<float, 4>>()}});
            }
            validate_material_parameter_overrides(value.material_parameters);
        }
        if (p.contains("material_variant") && !p.at("material_variant").is_null())
            value.material_variant.id = p.at("material_variant").get<AssetId>();
        value.enabled = p.at("enabled");
        value.visible = p.at("visible");
        value.cast_shadows = p.at("cast_shadows");
        value.receive_shadows = p.at("receive_shadows");
        value.layers = p.at("layers").get<std::uint32_t>();
        return value;
    } else if constexpr (std::is_same_v<T, ParticleEmitter>) {
        return particle_emitter_value(p);
    } else if constexpr (std::is_same_v<T, SpriteRenderer>) {
        return sprite_renderer_value(p);
    } else if constexpr (std::is_same_v<T, LocalScale>) {
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
    else if constexpr (std::is_same_v<T, NavigationAvoidance>)
        return T{p.at("enabled"), p.at("horizon")};
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
    } else if constexpr (std::is_same_v<T, AnimationSequence>) {
        AnimationSequence v;
        v.duration = p.at("duration");
        v.enabled = p.at("enabled");
        v.play_on_start = p.at("play_on_start");
        v.loop = p.at("loop");
        v.playback_speed = p.at("playback_speed");
        if (!std::isfinite(v.duration) || v.duration < 1e-6 || v.duration > 3600 ||
            !std::isfinite(v.playback_speed) || v.playback_speed < 0 || v.playback_speed > 4)
            throw std::runtime_error("Animation Sequence duration/speed is outside its range");
        return v;
    } else if constexpr (std::is_same_v<T, CameraShot>) {
        CameraShot v;
        if (!p.at("sequence").is_null())
            v.sequence = p.at("sequence").get<EntityRef>();
        if (!p.at("camera").is_null())
            v.camera = p.at("camera").get<EntityRef>();
        v.start_seconds = p.at("start_seconds");
        v.end_seconds = p.at("end_seconds");
        v.enabled = p.at("enabled");
        if (!std::isfinite(v.start_seconds) || !std::isfinite(v.end_seconds) ||
            v.start_seconds < 0 || v.start_seconds >= v.end_seconds || v.end_seconds > 3600)
            throw std::runtime_error("Camera Shot requires 0 <= start < end <= 3600 seconds");
        return v;
    } else if constexpr (std::is_same_v<T, AudioCue>) {
        AudioCue v;
        if (!p.at("sequence").is_null())
            v.sequence = p.at("sequence").get<EntityRef>();
        if (!p.at("source").is_null())
            v.source = p.at("source").get<EntityRef>();
        v.time_seconds = p.at("time_seconds");
        v.enabled = p.at("enabled");
        if (!std::isfinite(v.time_seconds) || v.time_seconds < 0 || v.time_seconds > 3600)
            throw std::runtime_error("Audio Cue time must be between 0 and 3600 seconds");
        return v;
    } else if constexpr (std::is_same_v<T, TransformAnimator>) {
        TransformAnimator v;
        if (!p.at("clip").is_null())
            v.clip.id = p.at("clip").get<AssetId>();
        v.enabled = p.at("enabled");
        v.play_on_start = p.at("play_on_start");
        v.loop = p.at("loop");
        v.playback_speed = p.at("playback_speed");
        if (!p.value("sequence", Json()).is_null())
            v.sequence = p.at("sequence").get<EntityRef>();
        v.sequence_start_seconds = p.value("sequence_start_seconds", 0.);
        if (!std::isfinite(v.sequence_start_seconds) || v.sequence_start_seconds < 0 ||
            v.sequence_start_seconds > 3600)
            throw std::runtime_error("Sequence track start must be within 0–3600 seconds");
        return v;
    } else if constexpr (std::is_same_v<T, TwoBoneIK> || std::is_same_v<T, AimIK>) {
        T v;
        v.enabled = p.at("enabled");
        v.weight = p.at("weight");
        v.twist_degrees = p.at("twist_degrees");
        v.target_x = p.at("target_x");
        v.target_y = p.at("target_y");
        v.target_z = p.at("target_z");
        v.pole_x = p.at("pole_x");
        v.pole_y = p.at("pole_y");
        v.pole_z = p.at("pole_z");
        const auto ref = [&](const char* name) {
            return p.at(name).is_null() ? AssetId{} : p.at(name).get<AssetId>();
        };
        if constexpr (std::is_same_v<T, TwoBoneIK>) {
            v.start = {ref("start")};
            v.middle = {ref("middle")};
            v.end = {ref("end")};
            v.soften = p.at("soften");
            v.axis_x = p.at("axis_x");
            v.axis_y = p.at("axis_y");
            v.axis_z = p.at("axis_z");
        } else {
            v.joint = {ref("joint")};
            v.forward_x = p.at("forward_x");
            v.forward_y = p.at("forward_y");
            v.forward_z = p.at("forward_z");
            v.up_x = p.at("up_x");
            v.up_y = p.at("up_y");
            v.up_z = p.at("up_z");
        }
        return v;
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
        if (p.contains("blend_clip") && !p.at("blend_clip").is_null())
            v.blend_clip.id = p.at("blend_clip").get<AssetId>();
        v.blend_weight = p.value("blend_weight", 0.f);
        v.transition_seconds = p.value("transition_seconds", .25f);
        if (p.contains("states") && !p.at("states").is_null())
            v.states.id = p.at("states").get<AssetId>();
        v.state = p.value("state", std::uint32_t(0));
        v.root_motion = p.value("root_motion", std::uint32_t(0));
        if (p.contains("root_joint") && !p.at("root_joint").is_null())
            v.root_joint.id = p.at("root_joint").get<AssetId>();
        if (p.contains("blend_root") && !p.at("blend_root").is_null())
            v.blend_root.id = p.at("blend_root").get<AssetId>();
        v.blend_mode = p.value("blend_mode", std::uint32_t(0));
        if (p.contains("blend_mask") && !p.at("blend_mask").is_null())
            v.blend_mask.id = p.at("blend_mask").get<AssetId>();
        if (p.contains("blend_space") && !p.at("blend_space").is_null())
            v.blend_space.id = p.at("blend_space").get<AssetId>();
        v.blend_position = p.value("blend_position", 0.f);
        v.blend_position_y = p.value("blend_position_y", 0.f);
        if (!v.blend_clip.id && v.blend_weight != 0)
            throw std::runtime_error("Animator blend target needs a second clip");
        if (v.blend_root.id && !v.blend_clip.id)
            throw std::runtime_error("Animator blend subtree needs a second clip");
        if (v.blend_mask.id && !v.blend_clip.id)
            throw std::runtime_error("Animator bone mask needs a second clip");
        if (v.blend_mode > 1 || (v.blend_mode != 0 && !v.blend_clip.id))
            throw std::runtime_error("Animator blend mode is invalid or needs a second clip");
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
        v.attenuation = p.value("attenuation", 0u);
        v.rolloff = p.value("rolloff", 1.f);
        v.cone_enabled = p.value("cone_enabled", false);
        v.inner_angle = p.value("inner_angle", 60.f);
        v.outer_angle = p.value("outer_angle", 120.f);
        v.outer_gain = p.value("outer_gain", .25f);
        v.bus = p.value("bus", 0u);
        v.doppler = p.value("doppler", 0.f);
        if (v.maximum_distance < v.minimum_distance)
            throw std::runtime_error("Audio maximum distance must be at least minimum distance");
        if (v.outer_angle < v.inner_angle)
            throw std::runtime_error("Audio outer opening must be at least inner opening");
        return v;
    } else if constexpr (std::is_same_v<T, AudioListener>)
        return T{p.at("enabled")};
    else if constexpr (std::is_same_v<T, CharacterController>) {
        T v{p.at("enabled"),
            p.at("shape").get<std::uint32_t>(),
            p.at("radius"),
            p.at("height"),
            p.at("crouch_height"),
            p.at("mass"),
            p.at("max_strength"),
            p.at("max_slope"),
            p.at("step_height"),
            p.at("step_forward"),
            p.at("floor_probe"),
            p.at("gravity_factor"),
            p.at("layer").get<std::uint32_t>(),
            p.at("mask").get<std::uint32_t>()};
        if (v.crouch_height > v.height)
            throw std::runtime_error("Character crouch height exceeds standing height");
        return v;
    } else if constexpr (std::is_same_v<T, PhysicsBody>)
        return T{p.at("motion").get<std::uint32_t>(),
                 p.at("density"),
                 p.at("mass"),
                 p.at("friction"),
                 p.at("restitution"),
                 p.at("gravity_factor"),
                 p.value("enabled", true),
                 p.value("sensor", false),
                 p.value("layer", std::uint32_t{0}),
                 p.value("mask", UINT32_MAX),
                 p.value("continuous_collision", false),
                 p.value("linear_damping", .05f),
                 p.value("angular_damping", .05f),
                 p.value("max_linear_velocity", 500.f),
                 p.value("max_angular_velocity", 2700.f),
                 p.value("allow_sleeping", true)};
    else if constexpr (std::is_same_v<T, PhysicsJoint>)
        return physics_detail::joint_from_value(p);
    else if constexpr (std::is_same_v<T, AssetCollider>)
        return T{p.at("asset").is_null() ? AssetRef<CollisionAsset>{}
                                         : p.at("asset").get<AssetRef<CollisionAsset>>()};
    else if constexpr (std::is_same_v<T, SphereCollider>)
        return T{p.at("radius")};
    else if constexpr (std::is_same_v<T, CapsuleCollider> || std::is_same_v<T, CylinderCollider>)
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
    if constexpr (std::is_same_v<T, NodeVisibility>)
        c.template member<bool>("visible");
    else if constexpr (std::is_same_v<T, NodeSelectability>)
        c.template member<bool>("selectable");
    else if constexpr (std::is_same_v<T, Camera>) {
        c.template member<bool>("enabled")
            .template member<std::uint32_t>("projection")
            .template member<std::uint32_t>("basis")
            .template member<double>("vertical_fov")
            .template member<double>("orthographic_height")
            .template member<double>("orthographic_width")
            .template member<double>("near_plane")
            .template member<double>("far_plane")
            .template member<bool>("infinite_far")
            .template member<double>("aspect")
            .template member<bool>("flip_x")
            .template member<bool>("flip_y")
            .template member<double>("viewport_x")
            .template member<double>("viewport_y")
            .template member<double>("viewport_width")
            .template member<double>("viewport_height")
            .template member<std::int32_t>("order")
            .template member<std::uint32_t>("layers")
            .template member<bool>("clear_color")
            .template member<bool>("clear_depth")
            .template member<float>("background_r")
            .template member<float>("background_g")
            .template member<float>("background_b")
            .template member<float>("background_a");
        for (const auto& [field, offset] :
             {std::pair{"enabled", offsetof(Camera, enabled)},
              std::pair{"projection", offsetof(Camera, projection)},
              std::pair{"basis", offsetof(Camera, basis)},
              std::pair{"vertical_fov", offsetof(Camera, vertical_fov)},
              std::pair{"orthographic_height", offsetof(Camera, orthographic_height)},
              std::pair{"orthographic_width", offsetof(Camera, orthographic_width)},
              std::pair{"near_plane", offsetof(Camera, near_plane)},
              std::pair{"far_plane", offsetof(Camera, far_plane)},
              std::pair{"infinite_far", offsetof(Camera, infinite_far)},
              std::pair{"aspect", offsetof(Camera, aspect)},
              std::pair{"flip_x", offsetof(Camera, flip_x)},
              std::pair{"flip_y", offsetof(Camera, flip_y)},
              std::pair{"viewport_x", offsetof(Camera, viewport_x)},
              std::pair{"viewport_y", offsetof(Camera, viewport_y)},
              std::pair{"viewport_width", offsetof(Camera, viewport_width)},
              std::pair{"viewport_height", offsetof(Camera, viewport_height)},
              std::pair{"order", offsetof(Camera, order)},
              std::pair{"layers", offsetof(Camera, layers)},
              std::pair{"clear_color", offsetof(Camera, clear_color)},
              std::pair{"clear_depth", offsetof(Camera, clear_depth)},
              std::pair{"background_r", offsetof(Camera, background_r)},
              std::pair{"background_g", offsetof(Camera, background_g)},
              std::pair{"background_b", offsetof(Camera, background_b)},
              std::pair{"background_a", offsetof(Camera, background_a)}}) {
            const auto* member = ecs_struct_get_member(w.c_ptr(), c.id(), field);
            if (!member || std::size_t(member->offset) != offset)
                throw std::runtime_error("Camera native Meta disagrees with typed layout");
        }
    } else if constexpr (std::is_same_v<T, Light>) {
        c.template member<bool>("enabled")
            .template member<std::uint32_t>("kind")
            .template member<std::uint32_t>("basis")
            .template member<float>("color_r")
            .template member<float>("color_g")
            .template member<float>("color_b")
            .template member<double>("intensity")
            .template member<double>("range")
            .template member<double>("inner_cone")
            .template member<double>("outer_cone")
            .template member<bool>("cast_shadows")
            .template member<float>("shadow_bias")
            .template member<float>("shadow_normal_bias")
            .template member<std::uint32_t>("layers");
        for (const auto& [field, offset] :
             {std::pair{"enabled", offsetof(Light, enabled)},
              std::pair{"kind", offsetof(Light, kind)}, std::pair{"basis", offsetof(Light, basis)},
              std::pair{"color_r", offsetof(Light, color_r)},
              std::pair{"color_g", offsetof(Light, color_g)},
              std::pair{"color_b", offsetof(Light, color_b)},
              std::pair{"intensity", offsetof(Light, intensity)},
              std::pair{"range", offsetof(Light, range)},
              std::pair{"inner_cone", offsetof(Light, inner_cone)},
              std::pair{"outer_cone", offsetof(Light, outer_cone)},
              std::pair{"cast_shadows", offsetof(Light, cast_shadows)},
              std::pair{"shadow_bias", offsetof(Light, shadow_bias)},
              std::pair{"shadow_normal_bias", offsetof(Light, shadow_normal_bias)},
              std::pair{"layers", offsetof(Light, layers)}}) {
            const auto* member = ecs_struct_get_member(w.c_ptr(), c.id(), field);
            if (!member || std::size_t(member->offset) != offset)
                throw std::runtime_error("Light native Meta disagrees with typed layout");
        }
    } else if constexpr (std::is_same_v<T, ModelSource>) {
        register_asset_ref<ModelAsset>(w, "forge.model_ref");
        register_asset_ref<ModelNodeAsset>(w, "forge.model_node_ref");
        c.template member<AssetRef<ModelAsset>>("model").template member<AssetRef<ModelNodeAsset>>(
            "node");
        for (const auto& [field, offset] : {std::pair{"model", offsetof(ModelSource, model)},
                                            std::pair{"node", offsetof(ModelSource, node)}}) {
            const auto* member = ecs_struct_get_member(w.c_ptr(), c.id(), field);
            if (!member || std::size_t(member->offset) != offset)
                throw std::runtime_error("ModelSource native Meta disagrees with typed layout");
        }
    } else if constexpr (std::is_same_v<T, MeshRenderer>) {
        register_asset_ref<MeshAsset>(w, "forge.mesh_ref");
        register_asset_ref<MaterialVariantAsset>(w, "forge.material_variant_ref");
        register_asset_ref<MaterialAsset>(w, "forge.material_ref");
        w.component<std::string>("forge.authored_string").opaque(reflected_string);
        w.component<MaterialSlotOverride>("forge.material_slot_override")
            .template member<std::string>("slot")
            .template member<AssetRef<MaterialAsset>>("material");
        w.component<std::vector<MaterialSlotOverride>>("forge.material_slot_overrides")
            .opaque(reflected_vector<MaterialSlotOverride>);
        static_assert(sizeof(MaterialParameterType) == sizeof(std::int32_t));
        w.component<std::array<float, 4>>("forge.material_parameter_lanes")
            .template array<float>(4);
        w.component<MaterialParameter>("forge.material_parameter")
            .template member<std::int32_t>("type", 0, offsetof(MaterialParameter, type))
            .template member<std::array<float, 4>>("value", 0, offsetof(MaterialParameter, value));
        w.component<MaterialParameterOverride>("forge.material_parameter_override")
            .template member<std::string>("slot")
            .template member<std::string>("key")
            .template member<MaterialParameter>("parameter");
        w.component<std::vector<MaterialParameterOverride>>("forge.material_parameter_overrides")
            .opaque(reflected_vector<MaterialParameterOverride>);
        c.template member<AssetRef<MeshAsset>>("mesh")
            .template member<std::vector<MaterialSlotOverride>>("materials")
            .template member<bool>("enabled")
            .template member<bool>("visible")
            .template member<bool>("cast_shadows")
            .template member<bool>("receive_shadows")
            .template member<std::uint32_t>("layers")
            .template member<AssetRef<MaterialVariantAsset>>("material_variant")
            .template member<std::vector<MaterialParameterOverride>>("material_parameters");
    } else if constexpr (std::is_same_v<T, SpriteRenderer>) {
        register_asset_ref<MaterialAsset>(w, "forge.material_ref");
        c.template member<AssetRef<MaterialAsset>>("material")
            .template member<bool>("enabled")
            .template member<bool>("visible")
            .template member<bool>("flip_x")
            .template member<bool>("flip_y")
            .template member<std::uint32_t>("layers")
            .template member<std::uint32_t>("columns")
            .template member<std::uint32_t>("rows")
            .template member<std::uint32_t>("margin_x")
            .template member<std::uint32_t>("margin_y")
            .template member<std::uint32_t>("spacing_x")
            .template member<std::uint32_t>("spacing_y")
            .template member<std::uint32_t>("frame")
            .template member<std::int32_t>("order")
            .template member<double>("width")
            .template member<double>("height")
            .template member<double>("pivot_x")
            .template member<double>("pivot_y")
            .template member<float>("tint_r")
            .template member<float>("tint_g")
            .template member<float>("tint_b")
            .template member<float>("tint_a");
        for (const auto& [field, offset] :
             {std::pair{"material", offsetof(SpriteRenderer, material)},
              std::pair{"enabled", offsetof(SpriteRenderer, enabled)},
              std::pair{"visible", offsetof(SpriteRenderer, visible)},
              std::pair{"flip_x", offsetof(SpriteRenderer, flip_x)},
              std::pair{"flip_y", offsetof(SpriteRenderer, flip_y)},
              std::pair{"layers", offsetof(SpriteRenderer, layers)},
              std::pair{"columns", offsetof(SpriteRenderer, columns)},
              std::pair{"rows", offsetof(SpriteRenderer, rows)},
              std::pair{"margin_x", offsetof(SpriteRenderer, margin_x)},
              std::pair{"margin_y", offsetof(SpriteRenderer, margin_y)},
              std::pair{"spacing_x", offsetof(SpriteRenderer, spacing_x)},
              std::pair{"spacing_y", offsetof(SpriteRenderer, spacing_y)},
              std::pair{"frame", offsetof(SpriteRenderer, frame)},
              std::pair{"order", offsetof(SpriteRenderer, order)},
              std::pair{"width", offsetof(SpriteRenderer, width)},
              std::pair{"height", offsetof(SpriteRenderer, height)},
              std::pair{"pivot_x", offsetof(SpriteRenderer, pivot_x)},
              std::pair{"pivot_y", offsetof(SpriteRenderer, pivot_y)},
              std::pair{"tint_r", offsetof(SpriteRenderer, tint_r)},
              std::pair{"tint_g", offsetof(SpriteRenderer, tint_g)},
              std::pair{"tint_b", offsetof(SpriteRenderer, tint_b)},
              std::pair{"tint_a", offsetof(SpriteRenderer, tint_a)}}) {
            const auto* member = ecs_struct_get_member(w.c_ptr(), c.id(), field);
            if (!member || std::size_t(member->offset) != offset)
                throw std::runtime_error("SpriteRenderer native Meta disagrees with typed layout");
        }
    } else if constexpr (std::is_same_v<T, ParticleEmitter>) {
        register_asset_ref<ParticleEffectAsset>(w, "forge.particle_effect_ref");
        c.template member<AssetRef<ParticleEffectAsset>>("effect")
            .template member<bool>("enabled")
            .template member<bool>("visible")
            .template member<std::uint32_t>("seed")
            .template member<std::uint32_t>("layers")
            .template member<double>("playback_speed")
            .template member<double>("size_multiplier")
            .template member<float>("tint_r")
            .template member<float>("tint_g")
            .template member<float>("tint_b");
        for (const auto& [field, offset] :
             {std::pair{"effect", offsetof(ParticleEmitter, effect)},
              std::pair{"enabled", offsetof(ParticleEmitter, enabled)},
              std::pair{"visible", offsetof(ParticleEmitter, visible)},
              std::pair{"seed", offsetof(ParticleEmitter, seed)},
              std::pair{"layers", offsetof(ParticleEmitter, layers)},
              std::pair{"playback_speed", offsetof(ParticleEmitter, playback_speed)},
              std::pair{"size_multiplier", offsetof(ParticleEmitter, size_multiplier)},
              std::pair{"tint_r", offsetof(ParticleEmitter, tint_r)},
              std::pair{"tint_g", offsetof(ParticleEmitter, tint_g)},
              std::pair{"tint_b", offsetof(ParticleEmitter, tint_b)}}) {
            const auto* member = ecs_struct_get_member(w.c_ptr(), c.id(), field);
            if (!member || std::size_t(member->offset) != offset)
                throw std::runtime_error("ParticleEmitter native Meta disagrees with typed layout");
        }
    } else if constexpr (std::is_same_v<T, LocalTranslation>) {
        c.template member<double>("x").template member<double>("y").template member<double>("z");
    } else if constexpr (std::is_same_v<T, UiDocument>) {
        register_asset_ref<UiDocumentAsset>(w, "forge.ui_document_ref");
        c.template member<AssetRef<UiDocumentAsset>>("document")
            .template member<bool>("enabled")
            .template member<bool>("visible")
            .template member<std::uint32_t>("layer");
    } else if constexpr (std::is_same_v<T, NavigationSurface>)
        c.template member<bool>("enabled");
    else if constexpr (std::is_same_v<T, NavigationAvoidance>)
        c.template member<bool>("enabled").template member<float>("horizon");
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
    } else if constexpr (std::is_same_v<T, AnimationSequence>) {
        c.template member<double>("duration")
            .template member<bool>("enabled")
            .template member<bool>("play_on_start")
            .template member<bool>("loop")
            .template member<float>("playback_speed");
    } else if constexpr (std::is_same_v<T, CameraShot>) {
        (void)authoring_value_adapters(w);
        c.template member<EntityRef>("sequence")
            .template member<EntityRef>("camera")
            .template member<double>("start_seconds")
            .template member<double>("end_seconds")
            .template member<bool>("enabled");
    } else if constexpr (std::is_same_v<T, AudioCue>) {
        (void)authoring_value_adapters(w);
        c.template member<EntityRef>("sequence")
            .template member<EntityRef>("source")
            .template member<double>("time_seconds")
            .template member<bool>("enabled");
    } else if constexpr (std::is_same_v<T, TransformAnimator>) {
        (void)authoring_value_adapters(w);
        register_asset_ref<TransformAnimationAsset>(w, "forge.transform_animation_ref");
        c.template member<AssetRef<TransformAnimationAsset>>("clip")
            .template member<bool>("enabled")
            .template member<bool>("play_on_start")
            .template member<bool>("loop")
            .template member<float>("playback_speed")
            .template member<EntityRef>("sequence")
            .template member<double>("sequence_start_seconds");
    } else if constexpr (std::is_same_v<T, TwoBoneIK> || std::is_same_v<T, AimIK>) {
        register_asset_ref<ModelNodeAsset>(w, "forge.model_node_ref");
        if constexpr (std::is_same_v<T, TwoBoneIK>)
            c.template member<AssetRef<ModelNodeAsset>>("start")
                .template member<AssetRef<ModelNodeAsset>>("middle")
                .template member<AssetRef<ModelNodeAsset>>("end");
        else
            c.template member<AssetRef<ModelNodeAsset>>("joint");
        c.template member<bool>("enabled").template member<float>("weight");
        if constexpr (std::is_same_v<T, TwoBoneIK>)
            c.template member<float>("soften");
        c.template member<float>("twist_degrees")
            .template member<float>("target_x")
            .template member<float>("target_y")
            .template member<float>("target_z")
            .template member<float>("pole_x")
            .template member<float>("pole_y")
            .template member<float>("pole_z");
        if constexpr (std::is_same_v<T, TwoBoneIK>)
            c.template member<float>("axis_x")
                .template member<float>("axis_y")
                .template member<float>("axis_z");
        else
            c.template member<float>("forward_x")
                .template member<float>("forward_y")
                .template member<float>("forward_z")
                .template member<float>("up_x")
                .template member<float>("up_y")
                .template member<float>("up_z");
    } else if constexpr (std::is_same_v<T, Animator>) {
        register_asset_ref<SkeletonAsset>(w, "forge.skeleton_ref");
        register_asset_ref<AnimationClipAsset>(w, "forge.animation_clip_ref");
        register_asset_ref<AnimationStatesAsset>(w, "forge.animation_states_ref");
        register_asset_ref<AnimationMaskAsset>(w, "forge.animation_mask_ref");
        register_asset_ref<AnimationBlendSpaceAsset>(w, "forge.animation_blend_space_ref");
        register_asset_ref<ModelNodeAsset>(w, "forge.model_node_ref");
        c.template member<AssetRef<SkeletonAsset>>("skeleton")
            .template member<AssetRef<AnimationClipAsset>>("clip")
            .template member<bool>("enabled")
            .template member<bool>("play_on_start")
            .template member<bool>("loop")
            .template member<float>("playback_speed")
            .template member<AssetRef<AnimationClipAsset>>("blend_clip")
            .template member<float>("blend_weight")
            .template member<float>("transition_seconds")
            .template member<AssetRef<AnimationStatesAsset>>("states")
            .template member<std::uint32_t>("state")
            .template member<std::uint32_t>("root_motion")
            .template member<AssetRef<ModelNodeAsset>>("root_joint")
            .template member<AssetRef<ModelNodeAsset>>("blend_root")
            .template member<std::uint32_t>("blend_mode")
            .template member<AssetRef<AnimationMaskAsset>>("blend_mask")
            .template member<AssetRef<AnimationBlendSpaceAsset>>("blend_space")
            .template member<float>("blend_position")
            .template member<float>("blend_position_y");
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
            .template member<float>("maximum_distance")
            .template member<std::uint32_t>("attenuation")
            .template member<float>("rolloff")
            .template member<bool>("cone_enabled")
            .template member<float>("inner_angle")
            .template member<float>("outer_angle")
            .template member<float>("outer_gain")
            .template member<std::uint32_t>("bus")
            .template member<float>("doppler");
    } else if constexpr (std::is_same_v<T, AudioListener>)
        c.template member<bool>("enabled");
    else if constexpr (std::is_same_v<T, CharacterController>)
        c.template member<bool>("enabled")
            .template member<std::uint32_t>("shape")
            .template member<float>("radius")
            .template member<float>("height")
            .template member<float>("crouch_height")
            .template member<float>("mass")
            .template member<float>("max_strength")
            .template member<float>("max_slope")
            .template member<float>("step_height")
            .template member<float>("step_forward")
            .template member<float>("floor_probe")
            .template member<float>("gravity_factor")
            .template member<std::uint32_t>("layer")
            .template member<std::uint32_t>("mask");
    else if constexpr (std::is_same_v<T, PhysicsBody>)
        c.template member<std::uint32_t>("motion")
            .template member<float>("density")
            .template member<float>("mass")
            .template member<float>("friction")
            .template member<float>("restitution")
            .template member<float>("gravity_factor")
            .template member<bool>("enabled")
            .template member<bool>("sensor")
            .template member<std::uint32_t>("layer")
            .template member<std::uint32_t>("mask")
            .template member<bool>("continuous_collision")
            .template member<float>("linear_damping")
            .template member<float>("angular_damping")
            .template member<float>("max_linear_velocity")
            .template member<float>("max_angular_velocity")
            .template member<bool>("allow_sleeping");
    else if constexpr (std::is_same_v<T, PhysicsJoint>) {
        (void)authoring_value_adapters(w);
        c.template member<bool>("enabled")
            .template member<std::uint32_t>("kind")
            .template member<EntityRef>("body_a")
            .template member<EntityRef>("body_b")
            .template member<float>("anchor_a_x")
            .template member<float>("anchor_a_y")
            .template member<float>("anchor_a_z")
            .template member<float>("anchor_b_x")
            .template member<float>("anchor_b_y")
            .template member<float>("anchor_b_z")
            .template member<float>("min_distance")
            .template member<float>("max_distance")
            .template member<float>("min_travel")
            .template member<float>("max_travel")
            .template member<std::uint32_t>("axis_a")
            .template member<std::uint32_t>("normal_a")
            .template member<std::uint32_t>("axis_b")
            .template member<std::uint32_t>("normal_b")
            .template member<float>("min_angle")
            .template member<float>("max_angle")
            .template member<bool>("motor_enabled")
            .template member<float>("hinge_motor_speed")
            .template member<float>("slider_motor_speed")
            .template member<float>("max_motor_torque")
            .template member<float>("max_motor_force")
            .template member<std::uint32_t>("motor_mode")
            .template member<float>("hinge_motor_angle")
            .template member<float>("slider_motor_position")
            .template member<float>("motor_frequency")
            .template member<float>("motor_damping")
            .template member<bool>("collide_connected")
            .template member<float>("swing_normal_angle")
            .template member<float>("swing_plane_angle")
            .template member<float>("distance_spring_frequency")
            .template member<float>("distance_spring_damping");
    } else if constexpr (std::is_same_v<T, AssetCollider>) {
        register_asset_ref<CollisionAsset>(w, "forge.collision_ref");
        c.template member<AssetRef<CollisionAsset>>("asset");
    } else if constexpr (std::is_same_v<T, SphereCollider>)
        c.template member<float>("radius");
    else if constexpr (std::is_same_v<T, CapsuleCollider> || std::is_same_v<T, CylinderCollider>)
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
    if constexpr (std::is_same_v<T, MeshRenderer>) {
        for (const auto& [field, offset] :
             {std::pair{"mesh", offsetof(MeshRenderer, mesh)},
              std::pair{"materials", offsetof(MeshRenderer, materials)},
              std::pair{"enabled", offsetof(MeshRenderer, enabled)},
              std::pair{"visible", offsetof(MeshRenderer, visible)},
              std::pair{"cast_shadows", offsetof(MeshRenderer, cast_shadows)},
              std::pair{"receive_shadows", offsetof(MeshRenderer, receive_shadows)},
              std::pair{"layers", offsetof(MeshRenderer, layers)},
              std::pair{"material_variant", offsetof(MeshRenderer, material_variant)},
              std::pair{"material_parameters", offsetof(MeshRenderer, material_parameters)}}) {
            const auto* member = ecs_struct_get_member(w.c_ptr(), c.id(), field);
            if (!member || std::size_t(member->offset) != offset)
                throw std::runtime_error("MeshRenderer native Meta disagrees with typed layout");
        }
        for (const auto& [field, offset] :
             {std::pair{"slot", offsetof(MaterialSlotOverride, slot)},
              std::pair{"material", offsetof(MaterialSlotOverride, material)}}) {
            const auto* member =
                ecs_struct_get_member(w.c_ptr(), w.id<MaterialSlotOverride>(), field);
            if (!member || std::size_t(member->offset) != offset)
                throw std::runtime_error(
                    "MaterialSlotOverride native Meta disagrees with typed layout");
        }
        for (const auto& [field, offset] :
             {std::pair{"slot", offsetof(MaterialParameterOverride, slot)},
              std::pair{"key", offsetof(MaterialParameterOverride, key)},
              std::pair{"parameter", offsetof(MaterialParameterOverride, parameter)}}) {
            const auto* member =
                ecs_struct_get_member(w.c_ptr(), w.id<MaterialParameterOverride>(), field);
            if (!member || std::size_t(member->offset) != offset)
                throw std::runtime_error(
                    "Material parameter override native Meta disagrees with typed layout");
        }
    }
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
        descriptor<CharacterController>(
            "forge.character_controller",
            "Jolt character mechanics; feet origin, no game input bindings", "unitless", 0, 10000,
            [](flecs::world& w) {
                return register_type<CharacterController>(w, "forge.character_controller");
            }),
        descriptor<PhysicsBody>(
            "forge.physics_body", "Body motion: Static, Kinematic, Dynamic. Mass 0 uses density.",
            "unitless", 0, 1000000,
            [](flecs::world& w) { return register_type<PhysicsBody>(w, "forge.physics_body"); }),
        descriptor<PhysicsJoint>(
            "forge.physics_joint",
            "Connect two rigid bodies using distance, hinge, point, fixed or slider constraints",
            "unitless", {}, {},
            [](flecs::world& w) { return register_type<PhysicsJoint>(w, "forge.physics_joint"); }),
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
        descriptor<CylinderCollider>(
            "forge.cylinder_collider",
            "Y-axis cylinder radius and full height; X/Z scale magnitudes must match", "meters",
            .001, 10000,
            [](flecs::world& w) {
                return register_type<CylinderCollider>(w, "forge.cylinder_collider");
            }),
        descriptor<AssetCollider>(
            "forge.asset_collider", "Explicit reusable Collision asset; never a render Mesh alias",
            "unitless", {}, {},
            [](flecs::world& w) {
                return register_type<AssetCollider>(w, "forge.asset_collider");
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
        descriptor<AnimationSequence>(
            "forge.animation_sequence", "Coordinate object clips on one animation timeline",
            "unitless", {}, {},
            [](flecs::world& w) {
                return register_type<AnimationSequence>(w, "forge.animation_sequence");
            }),
        descriptor<CameraShot>(
            "forge.camera_shot", "Select a camera during a Scene sequence interval", "unitless", {},
            {}, [](flecs::world& w) { return register_type<CameraShot>(w, "forge.camera_shot"); }),
        descriptor<TransformAnimator>(
            "forge.transform_animator", "Authored local transform keyframes during Play",
            "unitless", {}, {},
            [](flecs::world& w) {
                return register_type<TransformAnimator>(w, "forge.transform_animator");
            }),
        descriptor<Animator>(
            "forge.animator",
            "Skeletal animation with an optional two-clip blend; derived poses only", "unitless",
            {}, {}, [](flecs::world& w) { return register_type<Animator>(w, "forge.animator"); }),
        descriptor<TwoBoneIK>(
            "forge.two_bone_ik", "Reach a model-space target after skeletal mixing", "unitless", {},
            {}, [](flecs::world& w) { return register_type<TwoBoneIK>(w, "forge.two_bone_ik"); }),
        descriptor<AimIK>("forge.aim_ik", "Aim a joint at a model-space target after reach IK",
                          "unitless", {}, {},
                          [](flecs::world& w) { return register_type<AimIK>(w, "forge.aim_ik"); }),
        descriptor<NavigationSurface>(
            "forge.navigation_surface",
            "Include static primitive geometry when building navigation", "unitless", {}, {},
            [](flecs::world& w) {
                return register_type<NavigationSurface>(w, "forge.navigation_surface");
            }),
        descriptor<NavigationAgent>(
            "forge.navigation_agent", "Fixed-tick path following for nonphysics entities",
            "unitless", {}, {},
            [](flecs::world&
                   w) { return register_type<NavigationAgent>(w, "forge.navigation_agent"); }),
        descriptor<NavigationAvoidance>(
            "forge.navigation_avoidance",
            "Predictive local steering between opted-in navigation agents", "unitless", {}, {},
            [](flecs::world& w) {
                return register_type<NavigationAvoidance>(w, "forge.navigation_avoidance");
            }),
        descriptor<UiDocument>(
            "forge.ui_document", "Runtime UI document displayed during Play", "unitless", {}, {},
            [](flecs::world& w) { return register_type<UiDocument>(w, "forge.ui_document"); }),
        descriptor<MeshRenderer>(
            "forge.mesh_renderer", "Mesh and logical material slot assignments", "unitless", {}, {},
            [](flecs::world& w) { return register_type<MeshRenderer>(w, "forge.mesh_renderer"); }),
        descriptor<SpriteRenderer>(
            "forge.sprite_renderer", "World-space image or uniform sheet using a shared material",
            "unitless", {}, {},
            [](flecs::world&
                   w) { return register_type<SpriteRenderer>(w, "forge.sprite_renderer"); }),
        descriptor<ParticleEmitter>(
            "forge.particle_emitter", "Reusable CPU mesh-particle effect driven by the fixed clock",
            "unitless", {}, {},
            [](flecs::world&
                   w) { return register_type<ParticleEmitter>(w, "forge.particle_emitter"); }),
        descriptor<ModelSource>(
            "forge.model_source", "Imported model and source-node provenance", "unitless", {}, {},
            [](flecs::world& w) { return register_type<ModelSource>(w, "forge.model_source"); }),
        descriptor<Camera>(
            "forge.camera", "Authored game camera; world pose is derived from independent TRS",
            "unitless", {}, {},
            [](flecs::world& w) { return register_type<Camera>(w, "forge.camera"); }),
        descriptor<Light>("forge.light", "Directional, point or spot light with physical intensity",
                          "unitless", {}, {},
                          [](flecs::world& w) { return register_type<Light>(w, "forge.light"); }),
        descriptor<NodeVisibility>(
            "forge.node_visibility",
            "Show meshes and lights in this structural subtree. Does not disable cameras, "
            "animation, physics or selection; a hidden ancestor still hides this entity.",
            "unitless", {}, {},
            [](flecs::world&
                   w) { return register_type<NodeVisibility>(w, "forge.node_visibility"); }),
        descriptor<NodeSelectability>(
            "forge.node_selectability",
            "Allow viewport selection in this structural subtree, independently of visibility. "
            "Hierarchy selection remains available; an unselectable ancestor still takes "
            "precedence.",
            "unitless", {}, {},
            [](flecs::world&
                   w) { return register_type<NodeSelectability>(w, "forge.node_selectability"); }),
        descriptor<AudioCue>(
            "forge.audio_cue", "Request an Audio Source one-shot at a Scene sequence time",
            "unitless", {}, {},
            [](flecs::world& w) { return register_type<AudioCue>(w, "forge.audio_cue"); })};
    return types;
}
Json registration_options(const Builtin& type, const std::string& field) {
    Json value = {{"description", type.description}, {"unit", type.unit}};
    if (type.minimum)
        value["minimum"] = *type.minimum;
    if (type.maximum)
        value["maximum"] = *type.maximum;
    const std::string name = type.name;
    if (name == "forge.mesh_renderer" && field == "material_variant") {
        value["description"] =
            "Imported material set from this mesh’s Model. None uses base materials. Explicit "
            "material slot overrides take precedence. Missing or foreign variants retain the "
            "previous usable draw and report an error.";
        value["nullable"] = true;
    }
    if (name == "forge.camera") {
        static const std::map<std::string, const char*> help = {
            {"enabled", "Include this camera in ordered Game view composition"},
            {"projection", "Perspective or orthographic projection"},
            {"basis", "FORGE looks along local +Z; imported glTF looks along local -Z without "
                      "changing node transforms"},
            {"vertical_fov",
             "Full vertical perspective field of view in radians, strictly between zero and pi"},
            {"orthographic_height", "Full orthographic view height in meters; must be positive"},
            {"orthographic_width", "Full orthographic width in meters; zero follows aspect, "
                                   "positive fits width/height without stretching"},
            {"near_plane", "Near clip distance in meters; perspective requires positive, "
                           "orthographic permits zero"},
            {"far_plane",
             "Far clip distance in meters, greater than near; ignored by infinite perspective"},
            {"infinite_far", "Perspective projection with no finite far clipping plane"},
            {"aspect", "Positive width/height ratio fits inside viewport; zero follows target. "
                       "Explicit orthographic width takes precedence"},
            {"flip_x", "Mirror projected horizontal image, including imported negative "
                       "orthographic magnification"},
            {"flip_y", "Mirror projected vertical image, including imported negative orthographic "
                       "magnification"},
            {"viewport_x", "Left edge as a fraction of target width"},
            {"viewport_y", "Top edge as a fraction of target height"},
            {"viewport_width",
             "Positive fraction of target width; rectangle must stay inside target"},
            {"viewport_height",
             "Positive fraction of target height; rectangle must stay inside target"},
            {"order",
             "Compose enabled cameras in ascending order; stable entity identity breaks ties"},
            {"layers", "Visible render layer bit mask"},
            {"clear_color", "Clear this camera viewport to its linear background color"},
            {"clear_depth",
             "Clear depth before this camera; disable only for deliberate composition"},
            {"background_r", "Linear background red channel"},
            {"background_g", "Linear background green channel"},
            {"background_b", "Linear background blue channel"},
            {"background_a", "Background alpha channel"}};
        value["description"] = help.at(field);
        if (field == "vertical_fov") {
            value["minimum"] = 0;
            value["maximum"] = std::numbers::pi;
            value["unit"] = "radians";
        }
        if (field.starts_with("orthographic_") || field.ends_with("_plane"))
            value["unit"] = "meters";
        if (field.starts_with("background_") || field.starts_with("viewport_")) {
            value["minimum"] = 0;
            value["maximum"] = 1;
        }
    }
    if (name == "forge.light") {
        static const std::map<std::string, const char*> help = {
            {"enabled", "Include this light in rendering"},
            {"kind", "Directional, point or spot light"},
            {"basis", "FORGE directional/spot forward is +Z; imported glTF forward is -Z"},
            {"color_r", "Linear light red multiplier from zero to one"},
            {"color_g", "Linear light green multiplier from zero to one"},
            {"color_b", "Linear light blue multiplier from zero to one"},
            {"intensity", "Nonnegative lux for directional lights or candela for point/spot "
                          "lights; unaffected by node scale"},
            {"range",
             "Point/spot distance cutoff in meters; zero is unbounded, independent of node scale"},
            {"inner_cone",
             "Spot half angle in radians where attenuation starts; less than outer angle"},
            {"outer_cone", "Spot half angle in radians where illumination ends; at most pi/2"},
            {"cast_shadows", "Request shadows; supported light types and budgets are renderer "
                             "profile capabilities"},
            {"shadow_bias", "Nonnegative shadow depth bias in normalized depth units"},
            {"shadow_normal_bias",
             "Nonnegative normal offset in world meters for shadow evaluation"},
            {"layers", "Affected render layer bit mask"}};
        value["description"] = help.at(field);
        if (field.starts_with("color_")) {
            value["minimum"] = 0;
            value["maximum"] = 1;
        }
        if (field.ends_with("_cone")) {
            value["minimum"] = 0;
            value["maximum"] = std::numbers::pi / 2;
            value["unit"] = "radians";
        }
        if (field == "range" || field == "shadow_normal_bias")
            value["unit"] = "meters";
    }
    if (name == "forge.asset_collider" && field == "asset") {
        value["asset_type"] = CollisionAsset::type;
        value["nullable"] = true;
    }
    if (name == "forge.character_controller") {
        if (field == "enabled") {
            value.erase("minimum");
            value.erase("maximum");
        }
        if (field == "shape") {
            value["maximum"] = 1;
            value["description"] = "Capsule (0) or Cylinder (1), with feet at local origin";
        }
        if (field == "radius" || field == "height" || field == "crouch_height" ||
            field == "step_forward")
            value["minimum"] = .001;
        if (field == "radius" || field.ends_with("height") || field == "step_forward" ||
            field == "floor_probe")
            value["unit"] = "meters";
        if (field == "mass") {
            value["minimum"] = .001;
            value["maximum"] = 1000000;
            value["unit"] = "kg";
        }
        if (field == "max_strength") {
            value["maximum"] = 1000000;
            value["description"] = "Maximum native pushing force in newtons; zero disables "
                                   "virtual-character push force";
        }
        if (field == "max_slope") {
            value["maximum"] = 90;
            value["description"] = "Maximum walkable slope angle in degrees";
        }
        if (field == "gravity_factor")
            value["maximum"] = 1000;
        if (field == "layer")
            value["maximum"] = 31;
        if (field == "mask")
            value["maximum"] = UINT32_MAX;
    }
    if (name == "forge.physics_joint") {
        if (field == "distance_spring_frequency") {
            value["minimum"] = 0;
            value["maximum"] = 100;
            value["unit"] = "Hz";
            value["display_name"] = "Spring Frequency";
            value["description"] = "Distance joint: 0 keeps hard limits; 0.01 to 100 Hz makes "
                                   "limits elastic. Frequency is normalized to effective mass.";
        }
        if (field == "distance_spring_damping") {
            value["minimum"] = 0;
            value["maximum"] = 10;
            value["unit"] = "ratio";
            value["display_name"] = "Spring Damping";
            value["description"] = "Distance spring damping ratio: below 1 oscillates, 1 is "
                                   "critical, above 1 is more damped. Inactive at frequency 0.";
        }
        if (field == "swing_normal_angle" || field == "swing_plane_angle") {
            value["minimum"] = 0;
            value["maximum"] = 180;
            value["unit"] = "degrees";
            value["description"] = "Swing Twist cone half-angle towards the frame normal "
                                   "(plane cross twist) or plane axis; below 0.5 degrees locks, "
                                   "above 179.5 degrees is free";
        }
        if (field.starts_with("axis_") || field.starts_with("normal_"))
            value["description"] = "Body-local axis; Swing Twist uses Axis as twist and Normal "
                                   "as plane axis. The two choices must differ.";
        if (field == "motor_mode")
            value["maximum"] = 1;
        if (field == "hinge_motor_angle" || field == "slider_motor_position") {
            const auto limit = field == "hinge_motor_angle" ? 180 : 10000;
            value["minimum"] = -limit;
            value["maximum"] = limit;
            value["unit"] = field == "hinge_motor_angle" ? "degrees" : "meters";
            value["description"] = "Position drive target relative to the admitted joint frame; "
                                   "enabled position drives require a target inside joint limits";
        }
        if (field == "motor_frequency") {
            value["minimum"] = .01;
            value["maximum"] = 100;
            value["unit"] = "Hz";
            value["description"] = "Position spring frequency; high frequencies have substantial "
                                   "fixed-step integration damping";
        }
        if (field == "motor_damping") {
            value["maximum"] = 10;
            value["unit"] = "ratio";
            value["description"] = "Position spring damping ratio: 1 is critical. Implicit "
                                   "integration also damps motion, including at ratio zero.";
        }
        if (field == "body_a" || field == "body_b") {
            value["nullable"] = true;
            value["description"] =
                "Distinct enabled rigid body in a loaded scene; at least one Dynamic";
        }
        if (field.starts_with("anchor_")) {
            value["minimum"] = -10000;
            value["maximum"] = 10000;
            value["unit"] = "meters";
            value["description"] =
                "Body-origin local anchor, multiplied by effective signed body scale";
        }
        if (field.ends_with("distance")) {
            value["minimum"] = .001;
            value["maximum"] = 10000;
            value["unit"] = "meters";
        }
        if (field == "min_travel" || field == "max_travel") {
            value["minimum"] = field == "min_travel" ? -10000 : 0;
            value["maximum"] = field == "min_travel" ? 0 : 10000;
            value["unit"] = "meters";
            value["description"] = "Signed slider displacement between anchors along Body A's axis";
        }
        if (field == "hinge_motor_speed" || field == "slider_motor_speed") {
            const auto limit = field == "hinge_motor_speed" ? 36000 : 1000;
            value["minimum"] = -limit;
            value["maximum"] = limit;
            value["unit"] = field == "hinge_motor_speed" ? "degrees/s" : "m/s";
            value["description"] = "Signed target speed of Body B relative to Body A; zero brakes "
                                   "with the configured budget. Body speed limits still apply.";
        }
        if (field == "max_motor_torque" || field == "max_motor_force") {
            value["minimum"] = 0;
            value["maximum"] = 1000000;
            value["unit"] = field == "max_motor_torque" ? "N*m" : "N";
            value["description"] =
                "Maximum motor effort in either direction; zero applies no drive.";
        }
        if (field == "min_angle" || field == "max_angle") {
            value["minimum"] = field == "min_angle" ? -180 : 0;
            value["maximum"] = field == "min_angle" ? 0 : 180;
            value["unit"] = "degrees";
            value["description"] = "Hinge angle or Swing Twist twist bound relative to the "
                                   "body-local joint frames; degrees enclosing zero";
        }
    }
    if (name == "forge.physics_body") {
        if (field == "enabled" || field == "sensor" || field == "continuous_collision" ||
            field == "allow_sleeping") {
            value.erase("minimum");
            value.erase("maximum");
        }
        if (field == "layer") {
            value["maximum"] = 31;
            value["description"] = "Named project collision layer; stable slot 0..31";
        }
        if (field == "mask") {
            value["maximum"] = UINT32_MAX;
            value["description"] = "Collision layer mask; both bodies must permit the other layer";
        }
        if (field == "enabled")
            value["description"] = "Enable this body's collision and simulation";
        if (field == "sensor")
            value["description"] = "Report overlap contacts without solid collision response";
        if (field == "continuous_collision")
            value["description"] =
                "Extra linear collision sweep for fast Dynamic solids with positive native inner "
                "radius. Inactive for Static, Kinematic or sensor bodies; choice is preserved. "
                "Does not sweep angular motion and costs additional physics work.";
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
        if (field == "linear_damping" || field == "angular_damping") {
            value["minimum"] = 0;
            value["maximum"] = 10000;
            value["unit"] = "per second";
            value["description"] =
                "Dynamic velocity drag, applied once per physics step. Zero retains motion; "
                "large damping can stop motion in one step. Independent of surface friction.";
        }
        if (field == "max_linear_velocity" || field == "max_angular_velocity") {
            value["minimum"] = 0;
            value["maximum"] = 1000000;
            value["unit"] = field == "max_linear_velocity" ? "m/s" : "degrees/s";
            value["description"] =
                "Dynamic speed magnitude limit at integration. Zero prevents that motion. "
                "Solver contacts can temporarily exceed the integration limit.";
        }
        if (field == "allow_sleeping")
            value["description"] =
                "Permit automatic sleep when motion settles. Disabling keeps Dynamic bodies "
                "active and uses more physics work; it does not apply a force.";
    }
    if (name == "forge.audio_source") {
        static const std::map<std::string, const char*> help = {
            {"clip", "Registered AudioClip identity; null leaves this source unassigned"},
            {"bus", "Route through Master, Music, Effects or Dialogue volume. All channels "
                    "follow gameplay pause; changing channel preserves playback position"},
            {"play_on_start", "Start when this source is first realized; recovered autoplay "
                              "restarts at the beginning"},
            {"loop", "Repeat the clip after reaching its end"},
            {"gain", "Linear gain: zero is silent, one is nominal; values above one amplify"},
            {"pitch",
             "Playback speed and pitch multiplier: one is normal; no independent time stretching"},
            {"spatialized",
             "Use world position and one enabled listener; nonspatial sounds need no listener"},
            {"minimum_distance", "Closest distance used for the selected distance falloff"},
            {"maximum_distance", "Farthest clamped falloff distance; not a hard mute radius. Equal "
                                 "minimum/maximum disables distance loss"},
            {"attenuation", "Distance falloff curve; changes loudness with listener distance. "
                            "Spatialized controls positional sound independently"},
            {"rolloff", "Falloff strength: 1 is standard, 0 keeps loudness with distance while "
                        "preserving positional sound"},
            {"doppler", "Motion pitch from completed game ticks: zero disables, one is normal. "
                        "Relative source/listener motion changes pitch without restarting sound"},
            {"cone_enabled",
             "Project spatial sound toward the source's effective local negative Z; "
             "rotate the source or its parent to aim it"},
            {"inner_angle",
             "Full opening in degrees with full angular gain; 360 is omnidirectional"},
            {"outer_angle", "Full outer opening in degrees; must be at least the inner opening. "
                            "Angular gain fades between the two openings"},
            {"outer_gain", "Linear angular volume outside the outer opening, multiplied by "
                           "distance falloff and source gain; 0 is silent, 1 is unchanged"}};
        value["description"] = help.at(field);
        if (field == "bus") {
            value["display_name"] = "Mix channel";
        }
        if (field == "cone_enabled")
            value["display_name"] = "Directional sound";
        if (field == "inner_angle" || field == "outer_angle") {
            value["display_name"] =
                field == "inner_angle" ? "Inner opening (deg)" : "Outer opening (deg)";
            value["minimum"] = 0;
            value["maximum"] = 360;
        }
        if (field == "outer_gain") {
            value["display_name"] = "Outside volume";
            value["minimum"] = 0;
            value["maximum"] = 1;
        }
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
        if (field == "attenuation") {
            value["display_name"] = "Distance falloff";
        }
        if (field == "rolloff") {
            value["display_name"] = "Falloff strength";
            value["minimum"] = 0;
            value["maximum"] = 10;
        }
        if (field == "doppler") {
            value["display_name"] = "Doppler strength";
            value["minimum"] = 0;
            value["maximum"] = 5;
        }
        if (field.ends_with("distance")) {
            value["minimum"] = .001;
            value["maximum"] = 10000;
            value["unit"] = "meters";
        }
    }
    if (name == "forge.two_bone_ik" || name == "forge.aim_ik") {
        if (field == "start" || field == "middle" || field == "end" || field == "joint") {
            value["asset_type"] = ModelNodeAsset::type;
            value["nullable"] = true;
            value["description"] = "Durable joint in this holder's exact imported skeleton";
        } else if (field == "weight" || field == "soften") {
            value["minimum"] = 0;
            value["maximum"] = 1;
            value["description"] = field == "weight"
                                       ? "Zero bypasses IK; one applies full correction"
                                       : "Chain extension softening ratio";
        } else if (field == "twist_degrees") {
            value["minimum"] = -360;
            value["maximum"] = 360;
            value["unit"] = "degrees";
        } else if (field.starts_with("target_")) {
            value["minimum"] = -1000000;
            value["maximum"] = 1000000;
            value["unit"] = "meters";
            value["description"] =
                "Target coordinate in skeleton model space, before the holder transform";
        } else if (field != "enabled") {
            value["minimum"] = -10000;
            value["maximum"] = 10000;
            value["description"] =
                field.starts_with("pole_")
                    ? "Model-space pole direction; avoid alignment with the target"
                    : "Local direction; normalized when preparing IK";
        }
    }
    if (name == "forge.audio_cue") {
        if (field == "sequence") {
            value["display_name"] = "Sequence controller";
            value["description"] = "Scene Animation Sequence that owns this cue's timing";
        } else if (field == "source") {
            value["display_name"] = "Audio Source";
            value["description"] = "Audio Source in the same loaded Scene; owns clip and gain";
        } else if (field == "time_seconds") {
            value["display_name"] = "Time (seconds)";
            value["minimum"] = 0;
            value["maximum"] = 3600;
            value["unit"] = "seconds";
            value["description"] = "Forward-playback event inside the controller duration";
        }
    }
    if (name == "forge.camera_shot") {
        if (field == "sequence") {
            value["display_name"] = "Sequence controller";
            value["description"] = "Scene Animation Sequence that owns this shot's timing";
        } else if (field == "camera") {
            value["display_name"] = "Camera";
            value["description"] = "Camera in the same loaded Scene; selection never binds by name";
        } else if (field == "start_seconds" || field == "end_seconds") {
            value["display_name"] = field == "start_seconds" ? "Start (seconds)" : "End (seconds)";
            value["minimum"] = 0;
            value["maximum"] = 3600;
            value["unit"] = "seconds";
            value["description"] = "Nonoverlapping interval inside the controller duration";
        }
    }
    if (name == "forge.animator" || name == "forge.transform_animator" ||
        name == "forge.animation_sequence") {
        if (field == "duration") {
            value["minimum"] = .000001;
            value["maximum"] = 3600;
            value["description"] =
                "Sequence length in seconds; clips are held at their end or cut here";
        } else if (field == "sequence") {
            value["display_name"] = "Sequence controller";
            value["description"] =
                "Optional scene Animation Sequence that controls this clip's timing";
        } else if (field == "sequence_start_seconds") {
            value["display_name"] = "Sequence start (seconds)";
            value["minimum"] = 0;
            value["maximum"] = 3600;
            value["description"] =
                "Clip start on its sequence; first pose holds before start and last pose after end";
        }
        if (field == "blend_mode") {
            value["display_name"] = "Blend mode";
            value["description"] = "Blend mixes two absolute poses; Additive adds second-clip "
                                   "offsets relative to the admitted skeleton rest pose";
        } else if (field == "root_motion") {
            value["display_name"] = "Root motion";
            value["description"] =
                "Off keeps the animated root in its pose; Apply moves this model entity; Extract "
                "exposes the current fixed-tick delta to C++";
        } else if (field == "root_joint") {
            value["display_name"] = "Root joint";
            value["asset_type"] = ModelNodeAsset::type;
            value["nullable"] = true;
            value["description"] = "Durable imported skeleton root node. None selects the unique "
                                   "root; display names and array positions do not bind it";
        } else if (field == "blend_root") {
            value["display_name"] = "Blend subtree";
            value["asset_type"] = ModelNodeAsset::type;
            value["nullable"] = true;
            value["description"] = "Second clip affects this imported skeleton joint and its "
                                   "descendants. None blends the whole skeleton";
        } else if (field == "states") {
            value["asset_type"] = AnimationStatesAsset::type;
            value["nullable"] = true;
            value["description"] = "Optional named clip set; C++ selects its stable state ID";
        } else if (field == "blend_space") {
            value["display_name"] = "Blend space";
            value["asset_type"] = AnimationBlendSpaceAsset::type;
            value["nullable"] = true;
            value["description"] =
                "One or two numeric axes blend neighboring clips at a shared cycle phase";
        } else if (field == "blend_position" || field == "blend_position_y") {
            value["display_name"] =
                field == "blend_position" ? "Blend position X" : "Blend position Y";
            value["minimum"] = -1000000;
            value["maximum"] = 1000000;
            value["description"] =
                field == "blend_position"
                    ? "X coordinate in the animation blend space; the outer boundary clamps"
                    : "Y coordinate for a two-dimensional blend space; ignored by one-dimensional "
                      "spaces";
        } else if (field == "blend_mask") {
            value["display_name"] = "Bone mask";
            value["asset_type"] = AnimationMaskAsset::type;
            value["nullable"] = true;
            value["description"] =
                "Reusable second-clip bone influences, multiplied by Blend subtree";
        } else if (field == "state") {
            value["minimum"] = 0;
            value["maximum"] = UINT32_MAX;
            value["description"] =
                "Stable state ID in the selected set; zero selects its entry state";
        } else if (field == "skeleton" || field == "clip" || field == "blend_clip") {
            value["asset_type"] = field == "skeleton" ? SkeletonAsset::type
                                  : name == "forge.transform_animator"
                                      ? TransformAnimationAsset::type
                                      : AnimationClipAsset::type;
            value["nullable"] = true;
            value["description"] =
                field == "skeleton"
                    ? "Registered skeleton identity; clip must bind to this exact skeleton revision"
                : name == "forge.transform_animator"
                    ? "Reusable local-transform animation clip"
                    : "Registered clip identity produced for the chosen skeleton";
        } else if (field == "blend_weight" || field == "transition_seconds") {
            value["minimum"] = 0;
            value["maximum"] = field == "blend_weight" ? 1 : 10;
            value["description"] =
                field == "blend_weight"
                    ? "Target mix: zero is the primary clip, one is the second clip"
                    : "Seconds to reach a new blend target from the currently evaluated mix";
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
    if (name == "forge.sprite_renderer") {
        if (field == "material") {
            value["asset_type"] = MaterialAsset::type;
            value["nullable"] = true;
            value["description"] =
                "Shared unlit image material; owns the texture and alpha/depth settings";
        } else if (field == "width" || field == "height") {
            value["minimum"] = .0001;
            value["maximum"] = 10000;
            value["unit"] = "meters";
            value["description"] = "Image size before the entity world transform";
        } else if (field == "pivot_x" || field == "pivot_y") {
            value["minimum"] = 0;
            value["maximum"] = 1;
            value["description"] =
                "Normalized pivot measured from the image top-left; 0.5 centers it";
        } else if (field.starts_with("tint_")) {
            value["minimum"] = 0;
            value["maximum"] = 1;
            value["description"] = "Linear straight RGBA modulation; one preserves the channel";
        } else if (field == "columns" || field == "rows" || field.starts_with("margin_") ||
                   field.starts_with("spacing_")) {
            value["minimum"] = field == "columns" || field == "rows" ? 1 : 0;
            value["maximum"] = 16384;
            value["description"] =
                field == "columns" || field == "rows"
                    ? "Uniform sheet cell count; cells must contain whole pixels"
                    : "Symmetric outer margin or between-cell spacing in texture pixels";
        } else if (field == "frame") {
            value["minimum"] = 0;
            value["maximum"] = 16384 * 16384 - 1;
            value["description"] =
                "Integer frame from the top-left, across rows; must fit the grid";
        } else if (field == "order") {
            value["description"] =
                "Shared queue priority at equal camera depth; higher draws later";
        } else if (field == "flip_x" || field == "flip_y") {
            value["description"] = "Mirror the image without moving its geometry or pivot";
        } else if (field == "enabled" || field == "visible") {
            value["description"] = "Include the image in scene rendering";
        } else if (field == "layers") {
            value["description"] = "Camera visibility layer bit mask";
        }
    }
    if (name == "forge.particle_emitter") {
        if (field == "effect") {
            value["asset_type"] = ParticleEffectAsset::type;
            value["nullable"] = true;
            value["description"] =
                "Reusable CPU mesh effect; assign a published Particle Effect before Play";
        } else if (field == "playback_speed") {
            value["minimum"] = 0;
            value["maximum"] = 16;
            value["description"] = "Multiply fixed-clock emission time; zero freezes this emitter";
        } else if (field == "size_multiplier") {
            value["minimum"] = .01;
            value["maximum"] = 16;
            value["description"] = "Multiply particle mesh size after the lifetime curve; does "
                                   "not change motion or restart the effect";
        } else if (field == "tint_r" || field == "tint_g" || field == "tint_b") {
            value["minimum"] = 0;
            value["maximum"] = 1;
            value["description"] = "Linear RGB factor after the effect gradient; one preserves "
                                   "the channel, zero removes it; no clock restart";
        } else if (field == "seed")
            value["description"] = "Repeatable visual seed; changing it restarts this effect";
        else if (field == "enabled")
            value["description"] = "Start the effect; disabling clears its runtime emission age";
        else if (field == "visible")
            value["description"] = "Show particle meshes without stopping emission time";
    }
    if (name == "forge.navigation_avoidance") {
        if (field == "horizon") {
            value["minimum"] = .25;
            value["maximum"] = 5;
            value["unit"] = "seconds";
            value["description"] = "Predict nearby participating agents over this time; footprint "
                                   "uses the baked profile";
        } else
            value["description"] =
                "Join local avoidance; idle and paused participants remain obstacles";
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
            value["description"] = "Stop when the remaining waypoint route reaches this distance";
        }
        if (field.starts_with("destination_")) {
            value["minimum"] = -4090;
            value["maximum"] = 4090;
            value["unit"] = "meters";
            value["description"] = "Destination in world coordinates, +Y up";
        }
        if (field == "enabled")
            value["description"] = "Enable nonphysics movement during Play; add Navigation "
                                   "Avoidance for local steering";
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
    if (name == "radians")
        return EcsRadians;
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
ecs_entity_t builtin_enum(flecs::world& world, const std::string& component,
                          const std::string& field) {
    const char* type = nullptr;
    std::vector<const char*> names;
    if ((component == "forge.camera" || component == "forge.light") && field == "basis") {
        type = "forge.ViewBasis";
        names = {"ForgePositiveZ", "GltfNegativeZ"};
    } else if (component == "forge.camera" && field == "projection") {
        type = "forge.CameraProjection";
        names = {"Perspective", "Orthographic"};
    } else if (component == "forge.light" && field == "kind") {
        type = "forge.LightKind";
        names = {"Directional", "Point", "Spot"};
    } else if (component == "forge.audio_source" && field == "attenuation") {
        type = "forge.AudioAttenuation";
        names = {"Inverse", "Linear", "Exponential"};
    } else if (component == "forge.audio_source" && field == "bus") {
        type = "forge.AudioBus";
        names = {"Master", "Music", "Effects", "Dialogue"};
    } else if (component == "forge.animator" && field == "blend_mode") {
        type = "forge.AnimationBlendMode";
        names = {"Blend", "Additive"};
    } else if (component == "forge.animator" && field == "root_motion") {
        type = "forge.RootMotionMode";
        names = {"Off", "Apply", "Extract"};
    } else if (component == "forge.physics_joint" && field == "kind") {
        type = "forge.JointKind";
        names = {"Distance", "Hinge", "Point", "Fixed", "Slider", "Swing Twist"};
    } else if (component == "forge.physics_joint" && field == "motor_mode") {
        type = "forge.JointMotorMode";
        names = {"Velocity", "Position"};
    } else if (component == "forge.physics_joint" &&
               (field.starts_with("axis_") || field.starts_with("normal_"))) {
        type = "forge.JointAxis";
        names = {"X", "Y", "Z"};
    } else if (component == "forge.character_controller" && field == "shape") {
        type = "forge.CharacterShape";
        names = {"Capsule", "Cylinder"};
    } else
        return 0;
    if (auto existing = world.lookup(type))
        return existing;
    ecs_enum_desc_t desc{};
    desc.entity = world.entity(type);
    desc.underlying_type = world.id<std::uint32_t>();
    for (std::size_t i = 0; i < names.size(); ++i) {
        desc.constants[i].name = names[i];
        desc.constants[i].value_unsigned = i;
    }
    if (!ecs_enum_init(world, &desc))
        throw std::runtime_error("Builtin enum registration failed");
    return desc.entity;
}
void annotate_type(flecs::world& world, flecs::entity component, const Builtin& type) {
    const auto* structure = ecs_get(world.c_ptr(), component.id(), EcsStruct);
    if (!structure)
        throw std::runtime_error("Reflection metadata is missing");
    const int count = ecs_vec_count(&structure->members);
    // Copy names and descriptors before registration can reallocate EcsStruct storage.
    std::vector<std::string> names;
    std::vector<ecs_member_t> members;
    const auto* source = ecs_vec_first_t(&structure->members, ecs_member_t);
    for (int i = 0; i < count; ++i) {
        names.emplace_back(source[i].name);
        members.push_back(source[i]);
    }
    std::vector<ecs_member_t> annotated(count);
    for (int i = 0; i < count; ++i) {
        auto& m = annotated[i];
        m = members[i];
        m.name = names[i].c_str();
        m.use_offset = true;
        const auto options = registration_options(type, m.name);
        m.unit = field_unit(world, options.at("unit"));
        const auto render_kind = builtin_enum(world, type.name, names[i]);
        const bool enumeration =
            render_kind || std::string(type.name) == "forge.primitive" ||
            (std::string(type.name) == "forge.physics_body" && names[i] == "motion");
        if (enumeration)
            m.type = render_kind ? render_kind
                                 : enum_type(world, std::string(type.name) == "forge.primitive");
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
    std::stable_sort(annotated.begin(), annotated.end(),
                     [](const auto& a, const auto& b) { return a.offset < b.offset; });
    // The exact pinned API updates named members, without clearing the structure.
    // Its fixed descriptor is a batch cache, not a reflected-type member limit.
    // Keep physical ordering across batches for the zero-offset workaround above.
    for (int start = 0; start < count; start += ECS_MEMBER_DESC_CACHE_SIZE) {
        ecs_struct_desc_t desc{};
        desc.entity = component.id();
        desc.create_member_entities = true;
        const auto batch = std::min(count - start, ECS_MEMBER_DESC_CACHE_SIZE);
        std::copy_n(annotated.begin() + start, batch, desc.members);
        if (!ecs_struct_init(world.c_ptr(), &desc))
            throw std::runtime_error("Builtin member metadata registration failed");
    }
    for (int i = 0; i < count; ++i) {
        const auto* actual = ecs_struct_get_member(world.c_ptr(), component.id(), names[i].c_str());
        if (!actual || actual->offset != members[i].offset || actual->size != members[i].size)
            throw std::runtime_error("Native member metadata changed the typed component layout");
    }
    const std::string id = type.name;
    const auto display = id == "forge.ui_document"   ? std::string("UI Document")
                         : id == "forge.two_bone_ik" ? std::string("Two Bone IK")
                         : id == "forge.aim_ik"      ? std::string("Aim IK")
                                                     : friendly_name(id.substr(id.find('.') + 1));
    component.set_doc_name(display.c_str()).set_doc_brief(type.description);
    for (const auto& name : names) {
        auto member = component.lookup(name.c_str());
        const auto options = registration_options(type, name);
        auto help = options.at("description").get<std::string>();
        if (id == "forge.local_translation")
            help = "Local translation along the " + name + " axis, in meters.";
        if (id == "forge.physics_body" && name == "mass")
            help = "Mass in kilograms; zero computes mass from density and collider volume.";
        const auto label = options.value("display_name", friendly_name(name));
        member.set_doc_name(label.c_str()).set_doc_brief(help.c_str());
    }
}
const Json& validation_schema(const char* name) {
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
Json builtin_schema() {
    Json types = Json::array();
    for (const auto& type : builtins())
        types.push_back(validation_schema(type.name));
    return {{"version", 1}, {"components", std::move(types)}};
}
Json register_builtins(flecs::world& world, unsigned family) {
    world.import<flecs::units>();
    world.component<ReflectedSequenceKey>(reflected_sequence_key_type);
    Json components = Json::array();
    for (const auto& type : builtins()) {
        const std::string name = type.name;
        const unsigned category =
            name == "forge.ui_document"             ? 5u
            : name.starts_with("forge.navigation_") ? 4u
            : (name == "forge.animator" || name == "forge.transform_animator" ||
               name == "forge.animation_sequence" || name == "forge.camera_shot" ||
               name == "forge.audio_cue" || name == "forge.two_bone_ik" || name == "forge.aim_ik")
                ? 3u
            : name.starts_with("forge.audio_") ? 2u
            : (name == "forge.physics_body" || name == "forge.physics_joint" ||
               name == "forge.character_controller" || name.ends_with("_collider"))
                ? 1u
                : 0u;
        if (category != family)
            continue;
        const auto c = type.register_type(world);
        annotate_type(world, c, type);
        if (name == "forge.mesh_renderer") {
            c.lookup("materials").set<ReflectedSequenceKey>({"slot"});
            c.lookup("material_parameters").set<ReflectedSequenceKey>({"slot", {"key"}});
        }
        std::vector<ReflectedAdapter> references;
        if (name == "forge.physics_joint" || name == "forge.transform_animator" ||
            name == "forge.camera_shot" || name == "forge.audio_cue")
            references = authoring_value_adapters(world);
        // Native typed identity remains authoritative even when an earlier SDK
        // schema registered a reference before its builtin family used a label.
        auto add_reference = [&]<class T>() {
            if (const auto id = world.id_if_registered<AssetRef<T>>())
                references.push_back({id, "asset_ref", T::type});
        };
        add_reference.template operator()<ModelAsset>();
        add_reference.template operator()<ModelNodeAsset>();
        add_reference.template operator()<MaterialVariantAsset>();
        add_reference.template operator()<MeshAsset>();
        add_reference.template operator()<ParticleEffectAsset>();
        add_reference.template operator()<CollisionAsset>();
        add_reference.template operator()<MaterialAsset>();
        add_reference.template operator()<AudioClipAsset>();
        add_reference.template operator()<SkeletonAsset>();
        add_reference.template operator()<AnimationClipAsset>();
        add_reference.template operator()<AnimationStatesAsset>();
        add_reference.template operator()<AnimationMaskAsset>();
        add_reference.template operator()<AnimationBlendSpaceAsset>();
        add_reference.template operator()<TransformAnimationAsset>();
        add_reference.template operator()<NavMeshAsset>();
        add_reference.template operator()<UiDocumentAsset>();
        if (name == "forge.mesh_renderer") {
            references.push_back({world.id<std::string>(), "string"});
            references.push_back({world.id<std::vector<MaterialSlotOverride>>(), "vector"});
            references.push_back({world.id<std::vector<MaterialParameterOverride>>(), "vector"});
        }
        auto fields = reflected_type_schema(world, c.id(), references).at("fields");
        for (auto& field : fields) {
            const auto member = field.at("id").get<std::string>();
            field["property_id"] = name + "." + member;
            field["default"] = type.defaults.at(member);
        }
        const char* categories[] = {
            "Rendering / Transform", "Physics", "Audio", "Animation", "Navigation", "Runtime UI"};
        components.push_back(
            {{"id", type.name},
             {"display_name", ecs_doc_get_name(world.c_ptr(), c.id())},
             {"description", ecs_doc_get_brief(world.c_ptr(), c.id())},
             {"category", category == 0 && name != "forge.model_source" &&
                                  name != "forge.local_translation" &&
                                  name != "forge.local_rotation" && name != "forge.local_scale" &&
                                  name != "forge.primitive" && name != "forge.tint"
                              ? "Rendering"
                              : categories[category]},
             {"schema_version", 1},
             {"fields", fields},
             {"optional", category != 0 || name == "forge.mesh_renderer" ||
                              name == "forge.particle_emitter" || name == "forge.sprite_renderer" ||
                              name == "forge.camera" || name == "forge.light" ||
                              name == "forge.node_visibility" ||
                              name == "forge.node_selectability"}});
        std::lock_guard lock(catalog_mutex);
        auto validation = components.back();
        validation["type"] = "struct";
        validation_catalog.try_emplace(type.name, std::move(validation));
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
        case EcsI32: {
            std::int32_t v;
            std::memcpy(&v, ptr, sizeof(v));
            number = v;
            break;
        }
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
namespace {
Json admitted_builtin_source(const Builtin& type, const Json& source) {
    auto current = source;
    if (std::string_view(type.name) == "forge.transform_animator")
        for (const auto* field : {"sequence", "sequence_start_seconds"})
            if (!current.contains(field))
                current[field] = type.defaults.at(field);
    if (std::string_view(type.name) == "forge.particle_emitter")
        for (const auto* field : {"size_multiplier", "tint_r", "tint_g", "tint_b"})
            if (!current.contains(field))
                current[field] = type.defaults.at(field);
    if (std::string_view(type.name) == "forge.mesh_renderer") {
        if (!current.contains("material_variant"))
            current["material_variant"] = nullptr;
        if (!current.contains("material_parameters"))
            current["material_parameters"] = Json::array();
    }
    if (std::string_view(type.name) == "forge.animator")
        for (const auto* field :
             {"blend_clip", "blend_weight", "transition_seconds", "states", "state", "root_motion",
              "root_joint", "blend_root", "blend_mode", "blend_mask", "blend_space",
              "blend_position", "blend_position_y"})
            if (!current.contains(field))
                current[field] = type.defaults.at(field);
    if (std::string_view(type.name) == "forge.audio_source")
        for (const auto* field : {"attenuation", "rolloff", "cone_enabled", "inner_angle",
                                  "outer_angle", "outer_gain", "bus", "doppler"})
            if (!current.contains(field))
                current[field] = type.defaults.at(field);
    if (std::string_view(type.name) == "forge.physics_body")
        for (const auto* field :
             {"enabled", "sensor", "layer", "mask", "continuous_collision", "linear_damping",
              "angular_damping", "max_linear_velocity", "max_angular_velocity", "allow_sleeping"})
            if (!current.contains(field))
                current[field] = type.defaults.at(field);
    if (std::string_view(type.name) == "forge.physics_joint")
        for (const auto* field :
             {"min_travel", "max_travel", "motor_enabled", "hinge_motor_speed",
              "slider_motor_speed", "max_motor_torque", "max_motor_force", "motor_mode",
              "hinge_motor_angle", "slider_motor_position", "motor_frequency", "motor_damping",
              "collide_connected", "swing_normal_angle", "swing_plane_angle",
              "distance_spring_frequency", "distance_spring_damping"})
            if (!current.contains(field))
                current[field] = type.defaults.at(field);
    return current;
}
} // namespace
Json builtin_extensions(const Builtin& type, const Json& source) {
    const auto& schema = validation_schema(type.name);
    return reflected_extensions(schema, admitted_builtin_source(type, source));
}
Json canonical_builtin_value(const Builtin& type, const Json& source) {
    return std::visit([](const auto& value) { return encode(value); },
                      type.decode(admitted_builtin_source(type, source)));
}
Json merge_builtin_extensions(const Builtin& type, const Json& known, const Json& extensions) {
    const auto& schema = validation_schema(type.name);
    return merge_reflected_extensions(schema, known, extensions);
}
Json merge_builtin_property_extensions(const Builtin& type, const std::string& field,
                                       const Json& known, const Json& source) {
    const auto& schema = validation_schema(type.name);
    for (const auto& entry : schema.at("fields"))
        if (entry.at("id") == field)
            return merge_reflected_extensions(entry, known, reflected_extensions(entry, source));
    throw std::runtime_error("Unknown builtin property extension");
}
void validate_components(const Json& components) {
    for (const auto& type : builtins()) {
        if (!components.contains(type.name))
            continue;
        const auto data = admitted_builtin_source(type, components.at(type.name));
        const auto& schema = validation_schema(type.name);
        // Preserve the legacy opaque extension envelope. The bounded reflected
        // payload contains only this builtin's known fields, never unknown data.
        Json known = Json::object();
        for (const auto& field : schema.at("fields")) {
            const auto key = field.at("id").get<std::string>();
            known[key] = std::string_view(type.name) == "forge.mesh_renderer" &&
                                 key == "material_variant" && !data.contains(key)
                             ? Json(nullptr)
                             : data.at(key);
        }
        validate_reflected_json(schema, known);
        (void)type.decode(data); // Cross-member invariants remain FORGE-owned.
    }
}
} // namespace forge::detail
