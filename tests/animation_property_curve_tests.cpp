#include <cmath>
#include <forge/transform_animation.hpp>
#include <iostream>
#include <limits>
#include <numbers>
using namespace forge;
namespace {
void check(bool ok, const char* why) {
    if (!ok)
        throw std::runtime_error(why);
}
void near(double a, double b) {
    check(std::abs(a - b) < 1e-12, "Property curve numeric oracle failed");
}
template <class F> void rejects(F f) {
    bool failed = false;
    try {
        f();
    } catch (const std::exception&) {
        failed = true;
    }
    check(failed, "Invalid property curve admitted");
}
} // namespace
int main() {
    try {
        for (const auto& binding : animation_property_bindings()) {
            const auto p = binding.property;
            if (animation_property_discrete(p)) {
                auto discrete = TransformAnimationSource::create(AssetId::generate());
                discrete.add_property(p, 0);
                auto end = discrete.clip().keys.front().properties;
                end[unsigned(p)] = 2;
                discrete.set_key(animation_timebase, {}, false, {}, {}, end);
                for (const auto time : {0., 1., 250000., 999999.999})
                    near(discrete.clip().sample_properties(time)[unsigned(p)], 0);
                near(discrete.clip().sample_properties(animation_timebase)[unsigned(p)], 2);
                const auto before = discrete.document;
                rejects(
                    [&] { discrete.set_property_curve(discrete.clip().keys.front().id, p, {}); });
                rejects([&] { animation_property_curve_sample(p, 1); });
                check(discrete.document == before, "Discrete curve rejection changed source");
                continue;
            }
            double from = .1, to = 1;
            if (p == AnimationProperty::CameraFov) {
                from = std::numbers::pi / 6;
                to = std::numbers::pi / 2;
            }
            if (p == AnimationProperty::CameraHeight) {
                from = 1;
                to = 10;
            }
            if (p == AnimationProperty::AudioPitch || p == AnimationProperty::ParticleSize) {
                from = .5;
                to = 2;
            }
            auto scalar = TransformAnimationSource::create(AssetId::generate());
            scalar.add_property(p, from);
            auto end = scalar.clip().keys.front().properties;
            end[unsigned(p)] = to;
            scalar.set_key(animation_timebase, {}, false, {}, {}, end);
            const auto key = scalar.clip().keys.front().id;
            scalar.set_property_curve(key, p, {true, 0, 0});
            near(scalar.clip().sample_properties(250000)[unsigned(p)], from + (to - from) * .15625);
            const auto previous = scalar.document;
            rejects([&] {
                scalar.set_property_curve(key, p,
                                          {true, 0, std::numeric_limits<double>::infinity()});
            });
            check(scalar.document == previous, "Invalid scalar slope changed admitted source");
            scalar.set_tangent_preset(std::array{key}, TransformAnimationTangentPreset::Flat);
            check(scalar.clip().keys.front().curves.properties[unsigned(p)].cubic &&
                      scalar.document["version"] == 4,
                  "Vector preset erased property curves or downgraded source");
            const auto limits = animation_property_limits(p);
            const auto low = limits.minimum_exclusive ? 1e-6 : limits.minimum;
            const auto high = limits.maximum_exclusive
                                  ? std::nextafter(limits.maximum, limits.minimum)
                                  : limits.maximum;
            for (bool reverse : {false, true}) {
                auto bounded = TransformAnimationSource::create(AssetId::generate());
                bounded.add_property(p, reverse ? high : low);
                auto endpoint = bounded.clip().keys.front().properties;
                endpoint[unsigned(p)] = reverse ? low : high;
                bounded.set_key(animation_timebase, {}, false, {}, {}, endpoint);
                bounded.set_property_curve(bounded.clip().keys.front().id, p, {true, 0, 0});
                const auto range_clip = bounded.clip();
                for (unsigned sample = 0; sample <= 64; ++sample) {
                    const auto value = range_clip.sample_properties(double(animation_timebase) *
                                                                    sample / 64)[unsigned(p)];
                    validate_animation_property(p, value);
                    const double t = double(sample) / 64;
                    const auto eased = t * t * (3 - 2 * t);
                    const auto expected =
                        std::lerp(reverse ? high : low, reverse ? low : high, eased);
                    check(std::abs(value - expected) <= 1e-12 * std::max(1., std::abs(expected)),
                          "Typed boundary curve differs from independent smoothstep");
                }
            }
            if (limits.minimum_exclusive)
                rejects([&] { scalar.add_property(p, limits.minimum); });
            if (limits.maximum_exclusive)
                rejects([&] { scalar.add_property(p, limits.maximum); });
        }
        auto precise_height = TransformAnimationSource::create(AssetId::generate());
        precise_height.add_property(AnimationProperty::CameraHeight, 1e9);
        auto small_height = precise_height.clip().keys.front().properties;
        small_height[unsigned(AnimationProperty::CameraHeight)] = 1e-12;
        precise_height.set_key(animation_timebase, {}, false, {}, {}, small_height);
        precise_height.set_property_curve(precise_height.clip().keys.front().id,
                                          AnimationProperty::CameraHeight, {true, 0, 0});
        const auto near_end = precise_height.clip().sample_properties(std::nextafter(
            double(animation_timebase), 0.))[unsigned(AnimationProperty::CameraHeight)];
        check(near_end > 1e-13 && near_end < 1e-11 && float(near_end) > 0,
              "Cubic cancellation lost a valid positive native camera height");
        auto source = TransformAnimationSource::create(AssetId::generate());
        source.add_property(AnimationProperty::LightRed, .1);
        source.document["channels"] = nlohmann::json::array();
        const auto first = source.clip().keys.front().id;
        auto values = source.clip().keys.front().properties;
        values[unsigned(AnimationProperty::LightRed)] = 1;
        const auto last = source.set_key(animation_timebase, {}, false, {}, {}, values);
        const auto linear = source;
        near(linear.clip().sample_properties(250000)[unsigned(AnimationProperty::LightRed)], .325);
        source.set_property_curve(first, AnimationProperty::LightRed, {true, 0, 0});
        check(source.document.at("version") == 4, "Property curve did not promote source version");
        const auto clip = source.clip();
        near(clip.sample_properties(250000)[unsigned(AnimationProperty::LightRed)], .240625);
        near(clip.sample_properties(500000)[unsigned(AnimationProperty::LightRed)], .55);
        check(clip.sample_properties(std::nextafter(
                  double(animation_timebase), 0.))[unsigned(AnimationProperty::LightRed)] <= 1,
              "RGB cubic roundoff escaped native profile");
        near(clip.sample_properties(animation_timebase)[unsigned(AnimationProperty::LightRed)], 1);
        const auto cooked = decode_transform_animation(source.cook(), source.asset());
        near(cooked.sample_properties(250000)[unsigned(AnimationProperty::LightRed)], .240625);
        const auto before = source.document;
        rejects(
            [&] { source.set_property_curve(first, AnimationProperty::LightRed, {true, 0, 20}); });
        check(source.document == before, "Rejected overshoot changed source/history");
        auto legacy_extension = linear;
        legacy_extension.set_tangent_preset(std::array{first},
                                            TransformAnimationTangentPreset::Flat);
        legacy_extension.document["keys"][0]["curves"]["property_curves"] = "opaque legacy data";
        near(legacy_extension.clip().sample_properties(
                 250000)[unsigned(AnimationProperty::LightRed)],
             .325);
        const auto original_legacy = legacy_extension.document;
        const auto envelope_bytes = legacy_extension.cook();
        const auto envelope = nlohmann::json::parse(envelope_bytes);
        check(envelope.at("version") == 5 && envelope.at("document") == original_legacy,
              "Cooked envelope changed the source version or unknown extensions");
        auto malformed_envelope = envelope;
        malformed_envelope["version"] = 5.;
        const auto malformed_text = malformed_envelope.dump();
        rejects([&] {
            decode_transform_animation(std::as_bytes(std::span(malformed_text)),
                                       legacy_extension.asset());
        });
        near(decode_transform_animation(envelope_bytes, legacy_extension.asset())
                 .sample_properties(250000)[unsigned(AnimationProperty::LightRed)],
             .325);
        rejects([&] {
            legacy_extension.set_property_curve(first, AnimationProperty::LightRed, {true, 0, 0});
        });
        check(legacy_extension.document == original_legacy,
              "Property curve conversion overwrote a legacy extension collision");
        auto legacy_cook = original_legacy;
        legacy_cook["format"] = "forge.transform-clip";
        const auto legacy_text = legacy_cook.dump();
        near(decode_transform_animation(std::as_bytes(std::span(legacy_text)),
                                        legacy_extension.asset())
                 .sample_properties(250000)[unsigned(AnimationProperty::LightRed)],
             .325);
        legacy_extension.retime_duration(4 * animation_timebase);
        legacy_extension.add_property(AnimationProperty::CameraFov, std::numbers::pi / 3);
        legacy_extension.erase_property(AnimationProperty::LightRed);
        check(legacy_extension.document["keys"][0]["curves"]["property_curves"] ==
                  "opaque legacy data",
              "Legacy retiming/property removal changed an unknown extension");
        auto undeclared = source;
        undeclared.document["keys"][0]["curves"]["property_curves"]["forge.light.color_g"] = {
            {"interpolation", "cubic"}, {"incoming", 0}, {"outgoing", 0}};
        rejects([&] { undeclared.validate(); });
        auto held = source;
        held.document["keys"][0]["interpolation"] = "held";
        near(held.clip().sample_properties(999999)[unsigned(AnimationProperty::LightRed)], .1);
        source.document["keys"][0]["curves"]["property_curves"]["forge.light.color_r"]
                       ["custom.note"] = "retain";
        source.set_property_tangent_preset(std::array{first, last}, AnimationProperty::LightRed,
                                           TransformAnimationTangentPreset::Linear);
        near(source.clip().sample_properties(250000)[unsigned(AnimationProperty::LightRed)], .325);
        check(source.document["keys"][0]["curves"]["property_curves"]["forge.light.color_r"]
                             ["custom.note"] == "retain",
              "Property tangent edit erased unknown metadata");
        const auto duration = source.clip().duration;
        source.retime_duration(duration * 2);
        near(source.clip().sample_properties(500000)[unsigned(AnimationProperty::LightRed)], .325);
        near(source.clip()
                 .keys.front()
                 .curves.properties[unsigned(AnimationProperty::LightRed)]
                 .outgoing,
             .45);
        source.add_property(AnimationProperty::CameraFov, std::numbers::pi / 3);
        check(source.document["version"] == 4, "Adding binding downgraded property curves");
        source.erase_property(AnimationProperty::LightRed);
        check(source.clip().properties == animation_property_bit(AnimationProperty::CameraFov) &&
                  source.clip().keys.front().curves.property_mask == 0,
              "Removing binding left dangling curve settings");
        auto full_source = source;
        full_source.document["opaque.padding"] = "";
        const auto remaining = transform_animation_byte_limit - full_source.document.dump().size();
        full_source.document["opaque.padding"] = std::string(remaining, 'x');
        check(full_source.document.dump().size() == transform_animation_byte_limit,
              "Source byte-boundary fixture has wrong size");
        const auto full_cook = full_source.cook();
        check(full_cook.size() > transform_animation_byte_limit &&
                  full_cook.size() <= transform_animation_cooked_byte_limit,
              "Cooked envelope allowance lost a valid maximum-size source");
        check(decode_transform_animation(full_cook, source.asset()).asset == source.asset(),
              "Maximum-size enclosed source did not decode");
        full_source.document["opaque.padding"].get_ref<std::string&>() += 'x';
        rejects([&] { full_source.cook(); });
        std::cout << "Property cubic source4/cook4, independent "
                     "polynomial/roundoff/bounds/held/retime/metadata/version checks passed\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
