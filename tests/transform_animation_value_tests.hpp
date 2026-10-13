#pragma once
#include <limits>
namespace {
void animation_value_tests() {
    auto source = TransformAnimationSource::create(AssetId::generate());
    source.document["duration_us"] = 3 * animation_timebase;
    const auto first = source.clip().keys.front().id;
    TransformAnimationCurves curve;
    curve.cubic = true;
    curve.incoming.translation = {2, -1, .5};
    curve.outgoing.translation = {-1, .5, 2};
    curve.incoming.scale = {.5, -.25, 1};
    curve.outgoing.scale = {-.25, .5, 0};
    source.set_key(0, {{1, 2, -3}, {}, {1, 2, 3}}, false, first, curve);
    source.set_key(animation_timebase, {{3, -1, 4}, rotation_from_euler({20, 90, 0}), {3, 1, 2}},
                   true, {}, curve);
    auto linear = curve;
    linear.cubic = false;
    source.set_key(2 * animation_timebase,
                   {{-2, 1, 0}, rotation_from_euler({0, 180, 40}), {2, 3, 1}}, false, {}, linear);
    source.set_key(3 * animation_timebase,
                   {{4, 3, 2}, rotation_from_euler({30, 0, 10}), {4, 2, 3}});
    source.document["opaque"] = {{"keep", 77}};
    std::vector<AssetId> ids;
    for (auto& key : source.document["keys"]) {
        ids.push_back(key["id"].get<AssetId>());
        key["opaque"] = {{"translation", {9, 8, 7}}, {"keep", 77}};
        if (key.contains("curves")) {
            key["curves"]["opaque"] = "preserve";
            key["curves"]["incoming"]["opaque"] = {3, 2, 1};
        }
    }
    source.validate();
    const auto original = source.document;
    const Double3 pm{-2, .5, 0}, po{3, -1, 2}, sm{1.5, -2, 0};
    auto expected = original;
    for (auto& key : expected["keys"])
        for (unsigned axis = 0; axis < 3; ++axis) {
            key["translation"][axis] = key["translation"][axis].get<double>() * pm[axis] + po[axis];
            key["scale"][axis] = key["scale"][axis].get<double>() * sm[axis];
            if (key.contains("curves"))
                for (const char* side : {"incoming", "outgoing"}) {
                    key["curves"][side]["translation"][axis] =
                        key["curves"][side]["translation"][axis].get<double>() * pm[axis];
                    key["curves"][side]["scale"][axis] =
                        key["curves"][side]["scale"][axis].get<double>() * sm[axis];
                }
        }
    source.transform_key_values(ids, pm, po, sm);
    require(source.document == expected,
            "Value transform changed IDs/times/rotation/masks/opaque fields");
    const auto old_clip = TransformAnimationSource{original}.clip(), transformed = source.clip();
    const auto cooked = decode_transform_animation(source.cook(), source.asset());
    for (unsigned step = 0; step <= 200; ++step) {
        const auto time = double(step) / 200 * old_clip.duration;
        const auto a = old_clip.sample(time), b = transformed.sample(time), c = cooked.sample(time);
        const Double3 ap{a.translation.x, a.translation.y, a.translation.z},
            bp{b.translation.x, b.translation.y, b.translation.z},
            cp{c.translation.x, c.translation.y, c.translation.z},
            as{a.scale.x, a.scale.y, a.scale.z}, bs{b.scale.x, b.scale.y, b.scale.z},
            cs{c.scale.x, c.scale.y, c.scale.z};
        for (unsigned axis = 0; axis < 3; ++axis) {
            near(bp[axis], ap[axis] * pm[axis] + po[axis]);
            near(bs[axis], as[axis] * sm[axis]);
            near(cp[axis], bp[axis]);
            near(cs[axis], bs[axis]);
        }
        near(a.rotation.x, b.rotation.x);
        near(a.rotation.y, b.rotation.y);
        near(a.rotation.z, b.rotation.z);
        near(a.rotation.w, b.rotation.w);
    }
    source.transform_key_values(ids, {1, 1, 1}, {}, {1, 1, 1});
    require(source.document == expected, "Identity value transform changed source");
    auto selected = TransformAnimationSource{original}, reverse = selected;
    selected.transform_key_values(std::array{ids[0], ids[2]}, pm, po, sm);
    reverse.transform_key_values(std::array{ids[2], ids[0]}, pm, po, sm);
    require(selected.document == reverse.document &&
                selected.document["keys"][0] == expected["keys"][0] &&
                selected.document["keys"][2] == expected["keys"][2] &&
                selected.document["keys"][1] == original["keys"][1] &&
                selected.document["keys"][3] == original["keys"][3],
            "Partial value edit changed unselected keys or depends on selection order");
    auto rejection = [&](auto selection, Double3 p, Double3 o, Double3 s) {
        const auto before = source.document;
        rejects([&] { source.transform_key_values(selection, p, o, s); });
        require(source.document == before, "Rejected value transform partially committed");
    };
    rejection(std::array<AssetId, 0>{}, {1, 1, 1}, {}, {1, 1, 1});
    rejection(std::array{AssetId{}}, {1, 1, 1}, {}, {1, 1, 1});
    rejection(std::array{AssetId::generate()}, {1, 1, 1}, {}, {1, 1, 1});
    rejection(std::array{ids[0], ids[0]}, {1, 1, 1}, {}, {1, 1, 1});
    for (double invalid : {std::numeric_limits<double>::quiet_NaN(),
                           std::numeric_limits<double>::infinity(), 1e9 + 1}) {
        rejection(ids, {invalid, 1, 1}, {}, {1, 1, 1});
        rejection(ids, {1, 1, 1}, {0, invalid, 0}, {1, 1, 1});
        rejection(ids, {1, 1, 1}, {}, {1, 1, invalid});
    }
    rejection(ids, {1e9, 1, 1}, {}, {1, 1, 1});
    rejection(ids, {1, 1, 1}, {}, {10000, 1, 1});
    rejection(ids, {1, 1, 1}, {}, {1e-100, 1, 1});
    auto reject_candidate = [&](TransformAnimationSource candidate, Double3 p, Double3 o,
                                Double3 s) {
        const auto before = candidate.document;
        std::vector<AssetId> selected;
        for (const auto& key : candidate.clip().keys)
            selected.push_back(key.id);
        rejects([&] { candidate.transform_key_values(selected, p, o, s); });
        require(candidate.document == before, "Invalid value/slopes/curve interior changed source");
    };
    auto peak = TransformAnimationSource::create(AssetId::generate());
    curve.incoming.translation = {-10, 0, 0};
    curve.outgoing.translation = {10, 0, 0};
    curve.incoming.scale = {-10, 0, 0};
    curve.outgoing.scale = {10, 0, 0};
    const LocalTransform high{{1e9 - 10, 0, 0}, {}, {9990, 1, 1}};
    peak.set_key(0, high, false, peak.clip().keys.front().id, curve);
    peak.set_key(2 * animation_timebase, high, false, {}, curve);
    near(peak.clip().sample(animation_timebase).translation.x, 1e9 - 5);
    near(peak.clip().sample(animation_timebase).scale.x, 9995);
    reject_candidate(peak, {1, 1, 1}, {8, 0, 0}, {1, 1, 1});
    reject_candidate(peak, {1, 1, 1}, {}, {1.0008, 1, 1});
    auto steep = TransformAnimationSource::create(AssetId::generate());
    curve.incoming.translation = curve.outgoing.translation = {1e9, 0, 0};
    curve.incoming.scale = curve.outgoing.scale = {};
    steep.set_key(0, {}, false, steep.clip().keys.front().id, curve);
    steep.set_key(1000, {}, false, {}, curve);
    reject_candidate(steep, {2, 1, 1}, {}, {1, 1, 1});
    for (const char* channel : {"forge.local_translation", "forge.local_scale"}) {
        auto masked = TransformAnimationSource{original};
        masked.document["channels"] = {channel};
        masked.transform_key_values(ids, pm, po, sm);
        auto masked_expected = expected;
        masked_expected["channels"] = {channel};
        require(masked.document == masked_expected, "Value edit changed playback channel masks");
    }
    auto max = TransformAnimationSource::create(AssetId::generate());
    auto key = max.document["keys"][0];
    max.document["duration_us"] = 4096;
    max.document["keys"] = Json::array();
    ids.clear();
    for (unsigned i = 0; i < 4096; ++i) {
        const auto id = AssetId::generate();
        key["id"] = id;
        key["time_us"] = i;
        key["opaque"] = i;
        max.document["keys"].push_back(key);
        ids.push_back(id);
    }
    max.transform_key_values(ids, {-1, 0, 2}, {3, 2, 1}, {-2, 0, .5});
    require(max.clip().keys.size() == 4096 && max.document["keys"][4095]["opaque"] == 4095 &&
                max.document["keys"][4095]["translation"] == Json({3, 2, 1}) &&
                !max.document["keys"][4095].contains("curves"),
            "Maximum profile value transform lost metadata or synthesized curves");
}
} // namespace
