#include "bounded_json.hpp"
#include <algorithm>
#include <cmath>
#include <forge/engine_assets.hpp>
#include <forge/particle_effect_source.hpp>
#include <set>
namespace forge {
namespace {
const char* channel_name(ParticleLifetimeChannel channel) {
    switch (channel) {
    case ParticleLifetimeChannel::Size:
        return "size_curve";
    case ParticleLifetimeChannel::Color:
        return "color_gradient";
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
    auto values = document;
    values.erase("asset_id");
    std::set<AssetId> identities;
    for (const auto* name : {"size_curve", "color_gradient"}) {
        if (!values.contains(name))
            continue;
        auto& stops = values.at(name);
        if (document.at("version") != 2 || !stops.is_array() || stops.size() > 16)
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
    data.version = 2;
    auto cooked = encode_particle_effect(data);
    cooked["format"] = particle_effect_cooked_format;
    cooked["asset_id"] = asset();
    const auto text = cooked.dump();
    const auto bytes = std::as_bytes(std::span(text));
    return {bytes.begin(), bytes.end()};
}
void ParticleEffectSource::promote() {
    auto data = compiled();
    if (data.version == 2)
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
void ParticleEffectSource::enable_lifetime(ParticleLifetimeChannel channel, bool enabled) {
    const auto* name = channel_name(channel);
    auto candidate = *this;
    candidate.promote();
    const auto data = candidate.compiled();
    auto& stops = candidate.document[name];
    if (!enabled)
        stops = nlohmann::json::array();
    else if (stops.empty()) {
        const bool size = channel == ParticleLifetimeChannel::Size;
        stops = nlohmann::json::array(
            {source_stop(0,
                         size ? nlohmann::json(data.size_start) : nlohmann::json(data.color_start),
                         "linear"),
             source_stop(1, size ? nlohmann::json(data.size_end) : nlohmann::json(data.color_end),
                         "linear")});
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
        if (channel == ParticleLifetimeChannel::Size)
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
ParticleEffectData decode_cooked_particle_effect(std::span<const std::byte> bytes, AssetId id) {
    auto cooked = asset_detail::parse_bounded_json(bytes, particle_effect_byte_limit, 4096, 16);
    if (cooked.at("format") != particle_effect_cooked_format ||
        cooked.at("asset_id").get<AssetId>() != id || !id || cooked.contains("extensions"))
        throw std::runtime_error("Particle effect cooked format/identity/projection mismatch");
    cooked["format"] = "forge.particle_effect";
    cooked.erase("asset_id");
    return decode_particle_effect(cooked);
}
} // namespace forge
