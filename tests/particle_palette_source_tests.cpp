#include <forge/particle_effect_source.hpp>
#include <iostream>
#include <stdexcept>
using namespace forge;
namespace {
void check(bool value, const char* why) {
    if (!value)
        throw std::runtime_error(why);
}
template <class F> void rejects(F action) {
    bool refused = false;
    try {
        action();
    } catch (const std::exception&) {
        refused = true;
    }
    check(refused, "Invalid palette source was accepted");
}
} // namespace
int main() {
    try {
        for (unsigned old_version = 26; old_version <= 37; ++old_version) {
            auto source = ParticleEffectSource::create(AssetId::generate());
            const auto flags = particle_detail::value_profile(old_version)->flags;
            if (flags & particle_detail::Graph)
                source.set_graph_program({{AssetId::generate()}, {1, 1, 1}, 1});
            source.enable_gpu_sorted_alpha(true);
            if (flags & particle_detail::Billboard)
                source.enable_gpu_billboards(true);
            if (flags & particle_detail::Collision)
                source.set_gpu_collision(true);
            if (flags & particle_detail::Trail)
                source.set_gpu_trails(true);
            if (flags & particle_detail::Opacity)
                source.enable_gpu_opacity(true);
            const auto before = source.compiled();
            const auto cook = source.cook();
            check(before.version == old_version,
                  "Legacy profile fixture selected the wrong transport");
            source.document["extensions"] = {{"plugin.fixture", {1, 2, 3}}};
            const auto identity = source.asset();
            source.enable_gpu_material_palette(true);
            auto data = source.compiled();
            check(source.asset() == identity && data.version == old_version + 14 &&
                      source.document.at("version").get<unsigned>() == 17 &&
                      particle_uses_gpu_material_palette(data) &&
                      particle_uses_gpu_sorted_alpha(data),
                  "Palette activation changed identity or selected a wrong profile");
            data.version = before.version;
            check(encode_particle_effect(data) == encode_particle_effect(before),
                  "Material selection changed particle simulation or appearance values");
            const auto text = source.document.dump();
            const auto parsed = ParticleEffectSource::parse(std::as_bytes(std::span(text)));
            check(parsed.document == source.document && parsed.cook() == source.cook(),
                  "Palette source reopen changed opaque fields, stop identities or cook");
            source.set_evaluation(ParticleEvaluation::Cpu);
            check(!particle_uses_gpu_material_palette(source.compiled()) &&
                      source.document.at("gpu_material_palette_enabled").get<bool>(),
                  "Inactive CPU evaluation erased the retained Material selection policy");
            source.set_evaluation(flags & particle_detail::Graph ? ParticleEvaluation::Graph
                                                                 : ParticleEvaluation::Gpu);
            check(source.compiled().version == old_version + 14,
                  "GPU/Graph reactivation lost retained palette settings");
            source.enable_gpu_weighted_alpha(true);
            check(particle_uses_weighted_alpha(source.compiled()) &&
                      !particle_uses_gpu_material_palette(source.compiled()),
                  "Inactive palette policy replaced weighted transport");
            source.enable_gpu_sorted_alpha(true);
            check(source.compiled().version == old_version + 14,
                  "Sorted transport did not restore its retained palette policy");
            source.enable_gpu_material_palette(false);
            check(
                source.compiled().version == old_version && source.cook() == cook &&
                    source.document.at("extensions") ==
                        nlohmann::json{{"plugin.fixture", {1, 2, 3}}},
                "Palette opt-out changed an earlier profile's runtime bytes or opaque source data");
            auto malformed = parsed;
            malformed.document["gpu_material_palette_enabled"] = 1;
            rejects([&] { malformed.validate(); });
            malformed = parsed;
            malformed.document["version"] = 16;
            rejects([&] { malformed.validate(); });
        }
        auto retained = ParticleEffectSource::create(AssetId::generate());
        const auto untouched = retained.document;
        retained.enable_gpu_material_palette(false);
        check(retained.document == untouched,
              "Inactive policy unnecessarily upgraded an old source");
        retained.set_scene_collision(false, ParticleSceneCollision{});
        retained.enable_gpu_sorted_alpha(true);
        const auto old_cook = retained.cook();
        retained.document["extensions"] = {
            {"gpu_material_palette_enabled", "opaque older-format extension"}};
        retained.validate();
        check(retained.compiled().version == 26 && retained.cook() == old_cook,
              "An old source's opaque extension was reinterpreted as active policy");
        retained.enable_gpu_material_palette(true);
        retained.set_scene_collision(true);
        check(retained.document.at("version").get<unsigned>() == 17 &&
                  particle_uses_scene_collision(retained.compiled()),
              "Scene response downgraded the new source format");
        retained.set_evaluation(ParticleEvaluation::Gpu);
        check(particle_uses_gpu_material_palette(retained.compiled()),
              "Inactive Scene response erased GPU Material settings");
        auto precise = ParticleEffectSource::create(AssetId::generate());
        precise.enable_lifetime(ParticleLifetimeChannel::Size, true);
        precise.insert_lifetime_stop(ParticleLifetimeChannel::Size, .5);
        precise.insert_lifetime_stop(ParticleLifetimeChannel::Size, .5 + 1e-12);
        const auto before = precise.document;
        rejects([&] { precise.enable_gpu_material_palette(true); });
        check(precise.document == before, "Rejected GPU activation partially changed the source");
        std::cout << "Twelve explicit palette profiles, immutable old cooks, retained settings, "
                     "identity/reopen and atomic activation passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
