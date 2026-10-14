#include <cmath>
#include <forge/engine_assets.hpp>
#include <forge/particle_effect_source.hpp>
#include <iostream>
using namespace forge;
namespace {
void require(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
template <class F> void reject(F&& action) {
    try {
        action();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error("Malformed particle source accepted");
}
ParticleEffectSource parse(const nlohmann::json& document) {
    const auto text = document.dump();
    return ParticleEffectSource::parse(std::as_bytes(std::span(text)));
}
void legacy_and_promotion() {
    ParticleEffectData data;
    data.mesh = engine_primitive(0);
    auto legacy = ParticleEffectSource{encode_particle_effect(data)};
    legacy.document["asset_id"] = AssetId::generate();
    legacy.document["extensions"] = {{"author", {{"note", "original test"}}}};
    const auto original = legacy.document;
    legacy = parse(original);
    require(legacy.compiled() == data, "Legacy source reinterpreted its values");
    auto cooked = decode_cooked_particle_effect(legacy.cook(), legacy.asset());
    require(cooked.version == 2 && legacy.document == original,
            "Legacy cooking failed to normalize or changed the authored source");
    for (const auto seed : {0u, 7u, 0xffffffffu})
        require(sample_particle_effect(cooked, .9, seed) == sample_particle_effect(data, .9, seed),
                "Normalized legacy cooking changed sampling");
    // Existing immutable v1 cooks remain admitted without rewriting their bytes.
    auto old_cook = encode_particle_effect(data);
    old_cook["format"] = particle_effect_cooked_format;
    old_cook["asset_id"] = legacy.asset();
    const auto text = old_cook.dump();
    require(decode_cooked_particle_effect(std::as_bytes(std::span(text)), legacy.asset()) == data,
            "Old immutable particle cook rejected");
    legacy.promote();
    require(legacy.document.at("version") == 2 && legacy.document.at("profile") == "cpu_mesh2" &&
                legacy.document.at("extensions") == original.at("extensions") &&
                legacy.document.at("asset_id") == original.at("asset_id"),
            "Promotion lost source identity or opaque metadata");
    const auto promoted = legacy.document;
    legacy.promote();
    require(legacy.document == promoted, "Repeated promotion changed the source");
}
void lifetime_identity_and_projection() {
    auto source = ParticleEffectSource::create(AssetId::generate());
    source.document["size_start"] = .2;
    source.document["size_end"] = .8;
    source.document["color_start"] = {1, 0, 0};
    source.document["color_end"] = {0, 0, 1};
    source.enable_lifetime(ParticleLifetimeChannel::Size, true);
    source.enable_lifetime(ParticleLifetimeChannel::Color, true);
    const auto enabled = source.document;
    source.enable_lifetime(ParticleLifetimeChannel::Size, true);
    require(source.document == enabled, "Enabling an enabled channel replaced its identities");
    const auto inserted = source.insert_lifetime_stop(ParticleLifetimeChannel::Size, .25);
    const auto color = source.insert_lifetime_stop(ParticleLifetimeChannel::Color, .5);
    require(source.document.at("size_curve")[1].at("id").get<AssetId>() == inserted &&
                std::abs(source.compiled().size_curve[1].value - .35) < 1e-12 &&
                source.compiled().color_gradient[1].value == std::array{.5f, 0.f, .5f},
            "Inserted source stop did not retain interpolation or identity");
    source.document["size_curve"][0]["interpolation"] = "step";
    const auto stepped = source.insert_lifetime_stop(ParticleLifetimeChannel::Size, .125);
    require(source.document.at("size_curve")[1].at("value") == .2 &&
                source.document.at("size_curve")[1].at("interpolation") == "step",
            "Inserted Step stop changed the outgoing segment");
    const auto saved = source.document;
    auto reopened = parse(saved);
    require(reopened.document == saved, "Roundtrip changed local particle stop UUIDs");
    // Local identities belong to the root, so a duplicated root retains local UUIDs.
    reopened.document["asset_id"] = AssetId::generate();
    reopened.validate();
    require(reopened.document.at("size_curve") == saved.at("size_curve"),
            "A different root invalidated its independent local stop identities");
    const auto cooked = source.cook();
    const auto projection = nlohmann::json::parse(cooked.begin(), cooked.end());
    for (const auto* name : {"size_curve", "color_gradient"})
        for (const auto& stop : projection.at(name))
            require(!stop.contains("id"), "Cooked evaluator retained a source-only stop UUID");
    require(decode_cooked_particle_effect(cooked, source.asset()) == source.compiled(),
            "Cooked lifetime values differ from source projection");
    auto invalid = saved;
    invalid["color_gradient"][0]["id"] = saved.at("size_curve")[0].at("id");
    reject([&] { parse(invalid); });
    invalid = saved;
    invalid["size_curve"][0]["id"] = source.asset();
    reject([&] { parse(invalid); });
    invalid = saved;
    invalid["size_curve"][0].erase("id");
    reject([&] { parse(invalid); });
    invalid = saved;
    invalid["size_curve"][0]["id"] = "invalid UUID";
    reject([&] { parse(invalid); });
    invalid = saved;
    invalid["size_curve"][1]["time"] = 1;
    reject([&] { parse(invalid); });
    invalid = saved;
    invalid["color_gradient"][0]["unknown"] = true;
    reject([&] { parse(invalid); });
    const auto unchanged = source.document;
    reject([&] { source.insert_lifetime_stop(ParticleLifetimeChannel::Size, .25); });
    reject([&] { source.insert_lifetime_stop(ParticleLifetimeChannel::Size, 0); });
    reject([&] { source.insert_lifetime_stop(ParticleLifetimeChannel::Color, NAN); });
    reject([&] { source.remove_lifetime_stop(ParticleLifetimeChannel::Size, color); });
    reject([&] {
        source.remove_lifetime_stop(ParticleLifetimeChannel::Size,
                                    source.document.at("size_curve")[0].at("id").get<AssetId>());
    });
    require(source.document == unchanged, "Rejected lifetime mutation changed its source");
    source.remove_lifetime_stop(ParticleLifetimeChannel::Size, stepped);
    source.remove_lifetime_stop(ParticleLifetimeChannel::Size, inserted);
    source.enable_lifetime(ParticleLifetimeChannel::Size, false);
    require(source.compiled().size_curve.empty() && source.compiled().size_start == .2 &&
                source.compiled().size_end == .8 && !source.compiled().color_gradient.empty(),
            "Disabling a size curve lost fallback values or changed the color channel");
    source.enable_lifetime(ParticleLifetimeChannel::Size, true);
    for (unsigned i = 1; i < 15; ++i)
        source.insert_lifetime_stop(ParticleLifetimeChannel::Size, double(i) / 16);
    require(source.compiled().size_curve.size() == 16, "Maximum stop count failed");
    const auto full = source.document;
    reject([&] { source.insert_lifetime_stop(ParticleLifetimeChannel::Size, .99); });
    require(source.document == full, "Excess stop rejected after mutating the source");
}
} // namespace
int main() {
    try {
        legacy_and_promotion();
        lifetime_identity_and_projection();
        std::cout << "Legacy normalization, stable local stops and bounded projection passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
