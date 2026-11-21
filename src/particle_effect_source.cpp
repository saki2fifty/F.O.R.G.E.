#include "bounded_json.hpp"
#include <algorithm>
#include <cmath>
#include <forge/engine_assets.hpp>
#include <forge/particle_effect_source.hpp>
#include <set>
namespace forge {
namespace {
const char* cpu_alpha_profile(const nlohmann::json& document) {
    if (document.at("version").get<unsigned>() >= 18 &&
        document.value("scene_collision_2d", false) &&
        document.value("scene_collision_enabled", false))
        return document.value("billboard_enabled", false) ? "cpu_scene_collision_2d_billboard1"
                                                          : "cpu_scene_collision_2d1";
    if (document.value("scene_collision_enabled", false))
        return document.value("billboard_enabled", false) ? "cpu_scene_collision_billboard1"
                                                          : "cpu_scene_collision1";
    return document.value("collision_enabled", false)   ? "cpu_collision1"
           : document.value("trail_enabled", false)     ? "cpu_trail1"
           : document.value("billboard_enabled", false) ? "cpu_billboard1"
           : document.value("opacity_enabled", false)   ? "cpu_alpha2"
                                                        : "cpu_alpha1";
}
bool named_profile_has(std::string_view name, unsigned flags) {
    const auto* profile = particle_detail::value_profile(name);
    return profile && (profile->flags & flags) == flags;
}
bool cpu_alpha_profile(std::string_view profile) {
    return named_profile_has(profile, particle_detail::CpuAlpha);
}
bool gpu_profile(std::string_view profile) {
    return named_profile_has(profile, particle_detail::Gpu);
}
bool graph_profile(std::string_view profile) {
    return named_profile_has(profile, particle_detail::Graph);
}
const char* gpu_evaluation_profile(const nlohmann::json& document, bool graph) {
    using namespace particle_detail;
    unsigned flags = Gpu | (graph ? Graph : 0u);
    if (!graph && document.value("gpu_trail_enabled", false))
        flags |= Trail;
    else if (!graph && document.value("gpu_collision_enabled", false))
        flags |= Collision;
    else if (document.value("gpu_billboard_enabled", false))
        flags |= Billboard;
    if (document.value("gpu_sorted_enabled", false))
        flags |= Sorted;
    else if (document.value("gpu_weighted_enabled", false))
        flags |= Weighted;
    if ((flags & (Sorted | Weighted)) && document.value("gpu_opacity_enabled", false))
        flags |= Opacity;
    // New policy activates only in its explicitly introduced source format.
    const auto source_version = document.at("version").get<unsigned>();
    if (source_version >= 17 && (flags & Sorted) &&
        document.value("gpu_material_palette_enabled", false))
        flags |= MaterialPalette;
    for (const auto& profile : value_profiles)
        if (profile.flags == flags)
            return profile.name;
    throw std::runtime_error("Unsupported GPU particle evaluation combination");
}
void retain_opacity_values(nlohmann::json& document) {
    if (document.at("version").get<unsigned>() < 6) {
        document["opacity_enabled"] = false;
        document["opacity_start"] = 1;
        document["opacity_end"] = 0;
        document["opacity_curve"] = nlohmann::json::array();
    }
}
const char* channel_name(ParticleLifetimeChannel channel) {
    switch (channel) {
    case ParticleLifetimeChannel::Size:
        return "size_curve";
    case ParticleLifetimeChannel::Color:
        return "color_gradient";
    case ParticleLifetimeChannel::Opacity:
        return "opacity_curve";
    }
    throw std::runtime_error("Unknown particle lifetime channel");
}
nlohmann::json source_stop(double time, nlohmann::json value, const char* interpolation) {
    return {{"id", AssetId::generate()},
            {"time", time},
            {"value", std::move(value)},
            {"interpolation", interpolation}};
}
} // namespace
ParticleEffectSource ParticleEffectSource::create(AssetId id) {
    ParticleEffectData data;
    data.version = 2;
    data.mesh = engine_primitive(0);
    data.material = engine_material(EngineMaterial::LegacyBlockout);
    auto document = encode_particle_effect(data);
    document["asset_id"] = id;
    ParticleEffectSource source{std::move(document)};
    source.validate();
    return source;
}
ParticleEffectSource ParticleEffectSource::parse(std::span<const std::byte> bytes) {
    ParticleEffectSource source{
        asset_detail::parse_bounded_json(bytes, particle_effect_byte_limit, 4096, 16)};
    source.validate();
    return source;
}
AssetId ParticleEffectSource::asset() const { return document.at("asset_id").get<AssetId>(); }
ParticleEffectData ParticleEffectSource::compiled() const {
    if (!asset())
        throw std::runtime_error("Particle effect asset identity is empty");
    if (!document.at("version").is_number_integer())
        throw std::runtime_error("Invalid particle source version");
    const auto version = document.at("version").get<std::uint64_t>();
    auto values = document;
    values.erase("asset_id");
    std::set<AssetId> identities;
    for (const auto* name : {"size_curve", "color_gradient", "opacity_curve"}) {
        if (!values.contains(name))
            continue;
        auto& stops = values.at(name);
        if (version < 2 || !stops.is_array() || stops.size() > 16)
            throw std::runtime_error("Invalid particle source lifetime stops");
        for (auto& stop : stops) {
            if (!stop.is_object() || !stop.contains("id"))
                throw std::runtime_error("Particle source stop requires a local UUID");
            const auto id = stop.at("id").get<AssetId>();
            if (!id || id == asset() || !identities.insert(id).second)
                throw std::runtime_error("Duplicate or invalid particle source stop UUID");
            stop.erase("id");
        }
    }
    if (version > 18)
        throw std::runtime_error("Unknown Particle Effect source version");
    if (version < 4)
        return decode_particle_effect(values);
    // Source4–18 retain settings while another evaluation backend is active.
    // Only the active backend contributes runtime values and dependency edges.
    const auto profile = values.at("profile").get<std::string>();
    const auto* selected = particle_detail::value_profile(std::string_view(profile));
    // Source formats introduce contiguous immutable value-profile ranges.
    constexpr std::array maximum_values{0u,  0u,  0u,  0u,  4u,  5u,  6u,  7u,  9u, 10u,
                                        11u, 15u, 19u, 22u, 25u, 37u, 39u, 51u, 53u};
    if (!selected || selected->version < 2 || selected->version > maximum_values.at(version))
        throw std::runtime_error("Unknown Particle Effect evaluation profile " + profile +
                                 " for source version " + std::to_string(version));
    if (version >= 18) {
        if (!values.at("scene_collision_2d").is_boolean())
            throw std::runtime_error("Particle Scene collision space must be a boolean");
        if (values.at("scene_collision_2d").get<bool>())
            validate_particle_scene_collision_2d(
                decode_particle_scene_collision(values.at("scene_collision")));
        values.erase("scene_collision_2d");
    }
    if (version >= 17) {
        if (!values.at("gpu_material_palette_enabled").is_boolean())
            throw std::runtime_error("GPU Material palette activation must be a boolean");
        values.erase("gpu_material_palette_enabled");
    }
    if (version >= 16) {
        if (!values.at("scene_collision_enabled").is_boolean() ||
            (values.at("scene_collision_enabled").get<bool>() &&
             (values.at("collision_enabled").get<bool>() ||
              values.at("trail_enabled").get<bool>())))
            throw std::runtime_error(
                "Particle Scene collision activation conflicts with local plane or ribbons");
        (void)decode_particle_scene_collision(values.at("scene_collision"));
        values.erase("scene_collision_enabled");
        if (!(selected->flags & particle_detail::SceneCollision))
            values.erase("scene_collision");
    }
    if (version >= 15) {
        if (!values.at("gpu_sorted_enabled").is_boolean() ||
            (values.at("gpu_sorted_enabled").get<bool>() &&
             values.at("gpu_weighted_enabled").get<bool>()) ||
            (gpu_profile(std::string_view(profile)) &&
             profile != gpu_evaluation_profile(document, graph_profile(std::string_view(profile)))))
            throw std::runtime_error("GPU particle sorted alpha enabled state/profile mismatch");
        values.erase("gpu_sorted_enabled");
    }
    if (version >= 14) {
        if (!values.at("gpu_trail_enabled").is_boolean() ||
            (values.at("gpu_trail_enabled").get<bool>() &&
             (values.at("gpu_billboard_enabled").get<bool>() ||
              values.at("gpu_collision_enabled").get<bool>())) ||
            (gpu_profile(std::string_view(profile)) &&
             profile != gpu_evaluation_profile(document, graph_profile(std::string_view(profile)))))
            throw std::runtime_error("GPU particle trail enabled state/profile mismatch");
        values.erase("gpu_trail_enabled");
    }
    if (version >= 13) {
        if (!values.at("gpu_collision_enabled").is_boolean() ||
            (values.at("gpu_collision_enabled").get<bool>() &&
             values.at("gpu_billboard_enabled").get<bool>()) ||
            (gpu_profile(std::string_view(profile)) &&
             profile != gpu_evaluation_profile(document, graph_profile(std::string_view(profile)))))
            throw std::runtime_error("GPU particle plane enabled state/profile mismatch");
        values.erase("gpu_collision_enabled");
    }
    if (version >= 12) {
        if (!values.at("gpu_opacity_enabled").is_boolean() ||
            (gpu_profile(std::string_view(profile)) &&
             profile != gpu_evaluation_profile(document, graph_profile(std::string_view(profile)))))
            throw std::runtime_error("GPU particle opacity enabled state/profile mismatch");
        values.erase("gpu_opacity_enabled");
    }
    if (version >= 11) {
        if (!values.at("gpu_weighted_enabled").is_boolean() ||
            (gpu_profile(std::string_view(profile)) &&
             profile != gpu_evaluation_profile(document, graph_profile(std::string_view(profile)))))
            throw std::runtime_error("GPU particle weighted alpha enabled state/profile mismatch");
        values.erase("gpu_weighted_enabled");
    }
    if (version >= 10) {
        if (!values.at("collision_enabled").is_boolean())
            throw std::runtime_error("Particle collision enabled state must be boolean");
        (void)decode_particle_collision_plane(values.at("collision"));
        values.erase("collision_enabled");
        if (!(selected->flags & particle_detail::Collision))
            values.erase("collision");
    }
    if (version >= 9) {
        if (!values.at("trail_enabled").is_boolean())
            throw std::runtime_error("Particle trail enabled state must be boolean");
        (void)decode_particle_trail_settings(values.at("trail"));
        values.erase("trail_enabled");
        if (!(selected->flags & particle_detail::Trail))
            values.erase("trail");
    }
    if (version >= 8) {
        const auto& enabled = values.at("gpu_billboard_enabled");
        if (!enabled.is_boolean() ||
            (gpu_profile(std::string_view(profile)) &&
             profile != gpu_evaluation_profile(document, graph_profile(std::string_view(profile)))))
            throw std::runtime_error(
                "GPU particle camera alignment enabled state/profile mismatch");
        values.erase("gpu_billboard_enabled");
    }
    if (version >= 7) {
        if (!values.at("billboard_enabled").is_boolean())
            throw std::runtime_error("Particle camera alignment must be boolean");
        values.erase("billboard_enabled");
    }
    if (version >= 6) {
        const auto& enabled = values.at("opacity_enabled");
        if (!enabled.is_boolean() || (cpu_alpha_profile(std::string_view(profile)) &&
                                      profile != cpu_alpha_profile(document)))
            throw std::runtime_error("Particle appearance enabled state/profile mismatch");
        const bool opacity_enabled = enabled.get<bool>();
        values.erase("opacity_enabled");
        // Validate retained inactive opacity with CPU precision before projecting
        // the selected backend. It must never acquire GPU stop restrictions.
        auto opacity = values;
        opacity.erase("program");
        opacity.erase("trail");
        opacity.erase("collision");
        opacity.erase("scene_collision");
        opacity["version"] = 6;
        opacity["profile"] = "cpu_alpha2";
        (void)decode_particle_effect(opacity);
        if (!(selected->flags & particle_detail::Opacity))
            for (const auto* field : {"opacity_start", "opacity_end", "opacity_curve"})
                values.erase(field);
        if ((profile == "cpu_billboard1" || profile == "cpu_trail1" ||
             profile == "cpu_collision1" || (selected->flags & particle_detail::SceneCollision)) &&
            !opacity_enabled) {
            values["opacity_start"] = values["opacity_end"] = 1;
            values["opacity_curve"] = nlohmann::json::array();
        }
    }
    if (version == 4 || values.contains("program"))
        (void)decode_particle_graph_program(values.at("program"));
    if (!(selected->flags & particle_detail::Graph))
        values.erase("program");
    values["version"] = selected->version;
    if (selected->flags & (particle_detail::Billboard | particle_detail::Trail))
        values["mesh"] = engine_primitive(8);
    return decode_particle_effect(values);
}
void ParticleEffectSource::validate() const {
    (void)compiled();
    if (document.dump().size() > particle_effect_byte_limit)
        throw std::runtime_error("Particle effect source exceeds byte limit");
}
std::vector<std::byte> ParticleEffectSource::cook() const {
    validate();
    auto data = compiled();
    if (data.version == 1)
        data.version = 2;
    nlohmann::json cooked{{"format", particle_effect_cooked_format},
                          {"version", 3},
                          {"asset_id", asset()},
                          {"values", encode_particle_effect(data)}};
    const auto text = cooked.dump();
    const auto bytes = std::as_bytes(std::span(text));
    return {bytes.begin(), bytes.end()};
}
void ParticleEffectSource::promote() {
    auto data = compiled();
    if (data.version >= 2)
        return;
    data.version = 2;
    auto candidate = *this;
    const auto extended = encode_particle_effect(data);
    for (const auto* name : {"shape", "shape_size", "shape_radius", "drag", "size_curve",
                             "color_gradient", "version", "profile"})
        candidate.document[name] = extended.at(name);
    candidate.validate();
    document = std::move(candidate.document);
}
void ParticleEffectSource::set_evaluation(ParticleEvaluation evaluation) {
    if (evaluation != ParticleEvaluation::Cpu && evaluation != ParticleEvaluation::Gpu &&
        evaluation != ParticleEvaluation::Graph && evaluation != ParticleEvaluation::CpuAlpha)
        throw std::runtime_error("Unknown particle evaluation backend");
    auto candidate = *this;
    candidate.promote();
    if (evaluation == ParticleEvaluation::Graph && !candidate.graph_program())
        throw std::runtime_error(
            "Assign a particle graph Shader and bounds before selecting Graph");
    if (evaluation == ParticleEvaluation::CpuAlpha &&
        candidate.document.at("version").get<unsigned>() < 5)
        candidate.document["version"] = 5;
    else if (candidate.document.at("version").get<unsigned>() < 4)
        candidate.document["version"] = evaluation == ParticleEvaluation::Gpu ? 3 : 2;
    candidate.document["profile"] =
        evaluation == ParticleEvaluation::Graph ? gpu_evaluation_profile(candidate.document, true)
        : evaluation == ParticleEvaluation::Gpu ? gpu_evaluation_profile(candidate.document, false)
        : evaluation == ParticleEvaluation::CpuAlpha ? cpu_alpha_profile(candidate.document)
                                                     : "cpu_mesh2";
    candidate.validate();
    document = std::move(candidate.document);
}
void ParticleEffectSource::enable_opacity(bool enabled) {
    if (!enabled && !document.contains("opacity_enabled")) {
        validate();
        return;
    }
    auto candidate = *this;
    candidate.promote();
    const auto previous_profile = candidate.document.at("profile").get<std::string>();
    retain_opacity_values(candidate.document);
    if (candidate.document.at("version").get<unsigned>() < 6)
        candidate.document["version"] = 6;
    candidate.document["opacity_enabled"] = enabled;
    if (enabled || cpu_alpha_profile(std::string_view(previous_profile)))
        candidate.document["profile"] = cpu_alpha_profile(candidate.document);
    candidate.validate();
    document = std::move(candidate.document);
}
void ParticleEffectSource::enable_billboards(bool enabled) {
    if (!enabled && !document.contains("billboard_enabled")) {
        validate();
        return;
    }
    auto candidate = *this;
    candidate.promote();
    const auto previous_profile = candidate.document.at("profile").get<std::string>();
    retain_opacity_values(candidate.document);
    if (candidate.document.at("version").get<unsigned>() < 7)
        candidate.document["version"] = 7;
    candidate.document["billboard_enabled"] = enabled;
    if (enabled || cpu_alpha_profile(std::string_view(previous_profile)))
        candidate.document["profile"] = cpu_alpha_profile(candidate.document);
    candidate.validate();
    document = std::move(candidate.document);
}
void ParticleEffectSource::enable_gpu_billboards(bool enabled) {
    if (!enabled && !document.contains("gpu_billboard_enabled")) {
        validate();
        return;
    }
    auto candidate = *this;
    candidate.promote();
    const auto previous_profile = candidate.document.at("profile").get<std::string>();
    retain_opacity_values(candidate.document);
    if (candidate.document.at("version").get<unsigned>() < 7)
        candidate.document["billboard_enabled"] = false;
    if (candidate.document.at("version").get<unsigned>() < 8)
        candidate.document["version"] = 8;
    candidate.document["gpu_billboard_enabled"] = enabled;
    if (enabled && candidate.document.contains("gpu_collision_enabled"))
        candidate.document["gpu_collision_enabled"] = false;
    if (enabled && candidate.document.contains("gpu_trail_enabled"))
        candidate.document["gpu_trail_enabled"] = false;
    if (enabled || gpu_profile(std::string_view(previous_profile)))
        candidate.document["profile"] = gpu_evaluation_profile(
            candidate.document, graph_profile(std::string_view(previous_profile)));
    candidate.validate();
    document = std::move(candidate.document);
}
void ParticleEffectSource::enable_gpu_weighted_alpha(bool enabled) {
    if (!enabled && !document.contains("gpu_weighted_enabled")) {
        validate();
        return;
    }
    auto candidate = *this;
    candidate.promote();
    if (candidate.document.at("version").get<unsigned>() < 10)
        candidate.set_collision(false, ParticleCollisionPlane{});
    const auto previous_profile = candidate.document.at("profile").get<std::string>();
    candidate.document["version"] = std::max(11u, candidate.document.at("version").get<unsigned>());
    candidate.document["gpu_weighted_enabled"] = enabled;
    if (enabled && candidate.document.contains("gpu_sorted_enabled"))
        candidate.document["gpu_sorted_enabled"] = false;
    if (enabled || gpu_profile(std::string_view(previous_profile)))
        candidate.document["profile"] = gpu_evaluation_profile(
            candidate.document, graph_profile(std::string_view(previous_profile)));
    candidate.validate();
    document = std::move(candidate.document);
}
void ParticleEffectSource::enable_gpu_sorted_alpha(bool enabled) {
    if (!enabled && !document.contains("gpu_sorted_enabled")) {
        validate();
        return;
    }
    auto candidate = *this;
    candidate.promote();
    if (candidate.document.at("version").get<unsigned>() < 14) {
        const auto trail = candidate.document.contains("trail")
                               ? decode_particle_trail_settings(candidate.document.at("trail"))
                               : ParticleTrailSettings{};
        candidate.set_gpu_trails(false, trail);
    }
    const auto previous = candidate.document.at("profile").get<std::string>();
    candidate.document["version"] = std::max(15u, candidate.document.at("version").get<unsigned>());
    candidate.document["gpu_sorted_enabled"] = enabled;
    if (enabled)
        candidate.document["gpu_weighted_enabled"] = false;
    if (enabled || gpu_profile(std::string_view(previous)))
        candidate.document["profile"] =
            gpu_evaluation_profile(candidate.document, graph_profile(std::string_view(previous)));
    candidate.validate();
    document = std::move(candidate.document);
}
void ParticleEffectSource::enable_gpu_material_palette(bool enabled) {
    if (!enabled && !document.contains("gpu_material_palette_enabled")) {
        validate();
        return;
    }
    auto candidate = *this;
    candidate.promote();
    if (candidate.document.at("version").get<unsigned>() < 16)
        candidate.set_scene_collision(false, ParticleSceneCollision{});
    if (enabled)
        candidate.enable_gpu_sorted_alpha(true);
    const auto previous = candidate.document.at("profile").get<std::string>();
    candidate.document["version"] = std::max(17u, candidate.document.at("version").get<unsigned>());
    candidate.document["gpu_material_palette_enabled"] = enabled;
    if (gpu_profile(std::string_view(previous)))
        candidate.document["profile"] =
            gpu_evaluation_profile(candidate.document, graph_profile(std::string_view(previous)));
    candidate.validate();
    document = std::move(candidate.document);
}
void ParticleEffectSource::enable_gpu_opacity(bool enabled) {
    if (!enabled && !document.contains("gpu_opacity_enabled")) {
        validate();
        return;
    }
    auto candidate = *this;
    candidate.promote();
    const auto previous = candidate.document.at("profile").get<std::string>();
    if (enabled && !candidate.document.value("gpu_sorted_enabled", false))
        candidate.enable_gpu_weighted_alpha(true);
    candidate.document["version"] = std::max(12u, candidate.document.at("version").get<unsigned>());
    candidate.document["gpu_opacity_enabled"] = enabled;
    if (enabled || gpu_profile(std::string_view(previous)))
        candidate.document["profile"] =
            gpu_evaluation_profile(candidate.document, graph_profile(std::string_view(previous)));
    candidate.validate();
    document = std::move(candidate.document);
}
void ParticleEffectSource::set_trails(bool enabled, std::optional<ParticleTrailSettings> settings) {
    if (settings)
        validate_particle_trail_settings(*settings);
    if (!enabled && !settings && !document.contains("trail_enabled")) {
        validate();
        return;
    }
    auto candidate = *this;
    candidate.promote();
    const auto previous_profile = candidate.document.at("profile").get<std::string>();
    retain_opacity_values(candidate.document);
    if (candidate.document.at("version").get<unsigned>() < 7)
        candidate.document["billboard_enabled"] = false;
    if (candidate.document.at("version").get<unsigned>() < 8)
        candidate.document["gpu_billboard_enabled"] = false;
    candidate.document["version"] = std::max(9u, candidate.document.at("version").get<unsigned>());
    if (!settings)
        settings = candidate.document.contains("trail")
                       ? decode_particle_trail_settings(candidate.document.at("trail"))
                       : ParticleTrailSettings{};
    candidate.document["trail_enabled"] = enabled;
    if (enabled && candidate.document.contains("scene_collision_enabled"))
        candidate.document["scene_collision_enabled"] = false;
    candidate.document["trail"] = encode_particle_trail_settings(*settings);
    if (enabled || cpu_alpha_profile(std::string_view(previous_profile)))
        candidate.document["profile"] = cpu_alpha_profile(candidate.document);
    candidate.validate();
    document = std::move(candidate.document);
}
void ParticleEffectSource::set_collision(bool enabled,
                                         std::optional<ParticleCollisionPlane> plane) {
    if (plane)
        validate_particle_collision_plane(*plane);
    if (!enabled && !plane && !document.contains("collision_enabled")) {
        validate();
        return;
    }
    auto candidate = *this;
    candidate.promote();
    if (candidate.document.at("version").get<unsigned>() < 9)
        candidate.set_trails(false, ParticleTrailSettings{});
    const auto previous_profile = candidate.document.at("profile").get<std::string>();
    if (!plane)
        plane = candidate.document.contains("collision")
                    ? decode_particle_collision_plane(candidate.document.at("collision"))
                    : ParticleCollisionPlane{};
    candidate.document["version"] = std::max(10u, candidate.document.at("version").get<unsigned>());
    candidate.document["collision_enabled"] = enabled;
    if (enabled && candidate.document.contains("scene_collision_enabled"))
        candidate.document["scene_collision_enabled"] = false;
    candidate.document["collision"] = encode_particle_collision_plane(*plane);
    if (enabled || cpu_alpha_profile(std::string_view(previous_profile)))
        candidate.document["profile"] = cpu_alpha_profile(candidate.document);
    candidate.validate();
    document = std::move(candidate.document);
}
namespace {
void edit_scene_collision(ParticleEffectSource& source, std::optional<bool> enabled,
                          std::optional<ParticleSceneCollision> settings,
                          std::optional<bool> planar_2d) {
    if (settings)
        validate_particle_scene_collision(*settings);
    if (!enabled.value_or(false) && !settings &&
        !source.document.contains("scene_collision_enabled") && !planar_2d.value_or(false)) {
        source.validate();
        return;
    }
    auto candidate = source;
    candidate.promote();
    if (candidate.document.at("version").get<unsigned>() < 14) {
        const auto trail = candidate.document.contains("trail")
                               ? decode_particle_trail_settings(candidate.document.at("trail"))
                               : ParticleTrailSettings{};
        candidate.set_gpu_trails(false, trail);
    }
    if (candidate.document.at("version").get<unsigned>() < 15)
        candidate.document["gpu_sorted_enabled"] = false;
    const auto previous = candidate.document.at("profile").get<std::string>();
    if (!settings)
        settings = candidate.document.contains("scene_collision")
                       ? decode_particle_scene_collision(candidate.document.at("scene_collision"))
                       : ParticleSceneCollision{};
    candidate.document["version"] = std::max(16u, candidate.document.at("version").get<unsigned>());
    const bool active =
        enabled.value_or(candidate.document.value("scene_collision_enabled", false));
    candidate.document["scene_collision_enabled"] = active;
    candidate.document["scene_collision"] = encode_particle_scene_collision(*settings);
    if (planar_2d && (*planar_2d || candidate.document.contains("scene_collision_2d"))) {
        if (candidate.document.at("version").get<unsigned>() < 17)
            candidate.document["gpu_material_palette_enabled"] = false;
        candidate.document["version"] = 18;
        candidate.document["scene_collision_2d"] = *planar_2d;
    }
    if (active) {
        candidate.document["collision_enabled"] = false;
        candidate.document["trail_enabled"] = false;
    }
    if ((enabled && active) || cpu_alpha_profile(std::string_view(previous)))
        candidate.document["profile"] = cpu_alpha_profile(candidate.document);
    candidate.validate();
    source.document = std::move(candidate.document);
}
} // namespace
void ParticleEffectSource::set_scene_collision(bool enabled,
                                               std::optional<ParticleSceneCollision> settings) {
    edit_scene_collision(*this, enabled, settings, {});
}
void ParticleEffectSource::set_scene_collision(bool enabled,
                                               std::optional<ParticleSceneCollision> settings,
                                               bool planar_2d) {
    edit_scene_collision(*this, enabled, settings, planar_2d);
}
void ParticleEffectSource::set_scene_collision_space(bool planar_2d) {
    if (!planar_2d && !document.contains("scene_collision_2d")) {
        validate();
        return;
    }
    edit_scene_collision(*this, {}, {}, planar_2d);
}
void ParticleEffectSource::set_gpu_trails(bool enabled,
                                          std::optional<ParticleTrailSettings> settings) {
    if (settings)
        validate_particle_trail_settings(*settings);
    if (!enabled && !settings && !document.contains("gpu_trail_enabled")) {
        validate();
        return;
    }
    auto candidate = *this;
    candidate.promote();
    if (candidate.document.at("version").get<unsigned>() < 13) {
        const auto plane = candidate.document.contains("collision")
                               ? decode_particle_collision_plane(candidate.document.at("collision"))
                               : ParticleCollisionPlane{};
        candidate.set_gpu_collision(false, plane);
    }
    const auto previous = candidate.document.at("profile").get<std::string>();
    if (!settings)
        settings = decode_particle_trail_settings(candidate.document.at("trail"));
    candidate.document["version"] = std::max(14u, candidate.document.at("version").get<unsigned>());
    candidate.document["gpu_trail_enabled"] = enabled;
    candidate.document["trail"] = encode_particle_trail_settings(*settings);
    if (enabled) {
        candidate.document["gpu_billboard_enabled"] = false;
        candidate.document["gpu_collision_enabled"] = false;
    }
    if (enabled || gpu_profile(std::string_view(previous)))
        candidate.document["profile"] = gpu_evaluation_profile(
            candidate.document, !enabled && graph_profile(std::string_view(previous)));
    candidate.validate();
    document = std::move(candidate.document);
}
void ParticleEffectSource::set_gpu_collision(bool enabled,
                                             std::optional<ParticleCollisionPlane> plane) {
    if (plane)
        validate_particle_collision_plane(*plane);
    if (!enabled && !plane && !document.contains("gpu_collision_enabled")) {
        validate();
        return;
    }
    auto candidate = *this;
    candidate.promote();
    if (candidate.document.at("version").get<unsigned>() < 10)
        candidate.set_collision(false, plane.value_or(ParticleCollisionPlane{}));
    const auto previous = candidate.document.at("profile").get<std::string>();
    if (!plane)
        plane = decode_particle_collision_plane(candidate.document.at("collision"));
    if (candidate.document.at("version").get<unsigned>() < 11)
        candidate.document["gpu_weighted_enabled"] = false;
    if (candidate.document.at("version").get<unsigned>() < 12)
        candidate.document["gpu_opacity_enabled"] = false;
    candidate.document["version"] = std::max(13u, candidate.document.at("version").get<unsigned>());
    candidate.document["gpu_collision_enabled"] = enabled;
    candidate.document["collision"] = encode_particle_collision_plane(*plane);
    if (enabled) {
        candidate.document["gpu_billboard_enabled"] = false;
        if (candidate.document.contains("gpu_trail_enabled"))
            candidate.document["gpu_trail_enabled"] = false;
    }
    if (enabled || gpu_profile(std::string_view(previous)))
        candidate.document["profile"] = gpu_evaluation_profile(
            candidate.document, !enabled && graph_profile(std::string_view(previous)));
    candidate.validate();
    document = std::move(candidate.document);
}
void ParticleEffectSource::set_graph_program(const ParticleGraphProgram& program, bool activate) {
    auto candidate = *this;
    candidate.promote();
    if (candidate.document.at("version").get<unsigned>() < 4)
        candidate.document["version"] = 4;
    if (activate)
        candidate.document["profile"] = gpu_evaluation_profile(candidate.document, true);
    candidate.document["program"] = encode_particle_graph_program(program);
    candidate.validate();
    document = std::move(candidate.document);
}
std::optional<ParticleGraphProgram> ParticleEffectSource::graph_program() const {
    if (document.at("version").get<unsigned>() < 4 || !document.contains("program"))
        return {};
    return decode_particle_graph_program(document.at("program"));
}
void ParticleEffectSource::enable_lifetime(ParticleLifetimeChannel channel, bool enabled) {
    const auto* name = channel_name(channel);
    if (channel == ParticleLifetimeChannel::Opacity && !enabled && !document.contains(name)) {
        validate();
        return;
    }
    auto candidate = *this;
    candidate.promote();
    if (channel == ParticleLifetimeChannel::Opacity && enabled) {
        if (particle_uses_gpu(candidate.compiled()) &&
            candidate.document.value("gpu_opacity_enabled", false))
            candidate.enable_gpu_opacity(true);
        else
            candidate.enable_opacity(true);
    }
    const auto data = candidate.compiled();
    auto& stops = candidate.document[name];
    if (!enabled)
        stops = nlohmann::json::array();
    else if (stops.empty()) {
        const auto start =
            channel == ParticleLifetimeChannel::Size    ? nlohmann::json(data.size_start)
            : channel == ParticleLifetimeChannel::Color ? nlohmann::json(data.color_start)
                                                        : nlohmann::json(data.opacity_start);
        const auto end = channel == ParticleLifetimeChannel::Size ? nlohmann::json(data.size_end)
                         : channel == ParticleLifetimeChannel::Color
                             ? nlohmann::json(data.color_end)
                             : nlohmann::json(data.opacity_end);
        stops =
            nlohmann::json::array({source_stop(0, start, "linear"), source_stop(1, end, "linear")});
    }
    candidate.validate();
    document = std::move(candidate.document);
}
AssetId ParticleEffectSource::insert_lifetime_stop(ParticleLifetimeChannel channel, double time) {
    const auto* name = channel_name(channel);
    validate();
    if (!std::isfinite(time) || time <= 0 || time >= 1 || !document.contains(name))
        throw std::runtime_error("Particle stop must be inside an enabled lifetime channel");
    auto candidate = *this;
    auto& stops = candidate.document.at(name);
    if (stops.size() < 2 || stops.size() >= 16)
        throw std::runtime_error("Particle lifetime channel needs space for another stop");
    auto next = std::lower_bound(stops.begin(), stops.end(), time, [](const auto& stop, double at) {
        return stop.at("time").template get<double>() < at;
    });
    if (next == stops.end() || next->at("time") == time)
        throw std::runtime_error("Particle stop time already exists or has no segment");
    const auto& previous = *(next - 1);
    auto value = previous.at("value");
    const auto mode = previous.at("interpolation").get<std::string>();
    if (mode == "linear") {
        const auto a = previous.at("time").get<double>();
        const auto b = next->at("time").get<double>();
        const auto fraction = (time - a) / (b - a);
        if (channel != ParticleLifetimeChannel::Color)
            value = std::lerp(value.get<double>(), next->at("value").get<double>(), fraction);
        else
            for (unsigned axis = 0; axis < 3; ++axis)
                value[axis] = std::lerp(value[axis].get<double>(),
                                        next->at("value")[axis].get<double>(), fraction);
    }
    auto stop = source_stop(time, std::move(value), mode.c_str());
    const auto id = stop.at("id").get<AssetId>();
    stops.insert(next, std::move(stop));
    candidate.validate();
    document = std::move(candidate.document);
    return id;
}
void ParticleEffectSource::remove_lifetime_stop(ParticleLifetimeChannel channel, AssetId id) {
    const auto* name = channel_name(channel);
    validate();
    auto candidate = *this;
    auto& stops = candidate.document.at(name);
    const auto found = std::find_if(stops.begin(), stops.end(), [&](const auto& stop) {
        return stop.at("id").template get<AssetId>() == id;
    });
    if (found == stops.end() || found == stops.begin() || found == stops.end() - 1)
        throw std::runtime_error("Particle endpoints cannot be removed; select an interior stop");
    stops.erase(found);
    candidate.validate();
    document = std::move(candidate.document);
}
ParticleEffectData decode_cooked_particle_effect(std::span<const std::byte> bytes, AssetId id,
                                                 unsigned expected_envelope) {
    auto cooked = asset_detail::parse_bounded_json(bytes, particle_effect_byte_limit, 4096, 16);
    if (cooked.at("format") != particle_effect_cooked_format ||
        !cooked.at("version").is_number_integer() || cooked.at("asset_id").get<AssetId>() != id ||
        !id || cooked.contains("extensions"))
        throw std::runtime_error("Particle effect cooked format/identity/projection mismatch");
    const auto version = cooked.at("version").get<std::uint64_t>();
    if (expected_envelope && version != expected_envelope)
        throw std::runtime_error("Particle effect cooked envelope/recipe mismatch");
    if (version == 3) {
        if (cooked.size() != 4 || !cooked.contains("values") ||
            cooked.at("values").contains("extensions"))
            throw std::runtime_error("Particle cooked envelope has unsupported fields");
        const auto data = decode_particle_effect(cooked.at("values"));
        if (data.version < 2 || data.version > particle_detail::value_profiles.size())
            throw std::runtime_error("Particle cooked envelope has an unsupported value profile");
        return data;
    }
    cooked["format"] = "forge.particle_effect";
    cooked.erase("asset_id");
    const auto data = decode_particle_effect(cooked);
    if (data.version != 1 && data.version != 2)
        throw std::runtime_error("Legacy particle artifact has an unsupported value profile");
    return data;
}
} // namespace forge
