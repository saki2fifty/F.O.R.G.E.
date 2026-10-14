#include "bounded_json.hpp"
#include <forge/engine_assets.hpp>
#include <forge/particle_effect_source.hpp>
namespace forge {
ParticleEffectSource ParticleEffectSource::create(AssetId id) {
    ParticleEffectData data;
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
    return decode_particle_effect(values);
}
void ParticleEffectSource::validate() const {
    (void)compiled();
    if (document.dump().size() > particle_effect_byte_limit)
        throw std::runtime_error("Particle effect source exceeds byte limit");
}
std::vector<std::byte> ParticleEffectSource::cook() const {
    validate();
    auto cooked = encode_particle_effect(compiled());
    cooked["format"] = particle_effect_cooked_format;
    cooked["asset_id"] = asset();
    const auto text = cooked.dump();
    const auto bytes = std::as_bytes(std::span(text));
    return {bytes.begin(), bytes.end()};
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
