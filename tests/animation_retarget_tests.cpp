#include "animation_retarget.hpp"
#include <iostream>
#include <limits>

using namespace forge;
using namespace forge::animation_detail;
namespace {
void require(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
template <class F> void rejects(F run) {
    try {
        run();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error("Invalid retarget input was accepted");
}
void near(double a, double b) {
    require(std::isfinite(a) && std::abs(a - b) < 1e-5, "Retarget trajectory differs");
}
void vector(const auto& actual, std::array<double, 3> expected) {
    for (unsigned i = 0; i < 3; ++i)
        near(actual[i], expected[i]);
}
std::array<double, 4> quaternion(const LocalPose& value) {
    return {value.rotation[0], value.rotation[1], value.rotation[2], value.rotation[3]};
}
RetargetRig rig(unsigned count) {
    RetargetRig value;
    for (unsigned i = 0; i < count; ++i) {
        value.joints.push_back(AssetId::generate());
        value.parents.push_back(i ? std::int16_t(i - 1) : -1);
        value.rest.push_back({});
        value.rest.back().translation[1] = i ? 1.f : 0.f;
    }
    return value;
}
} // namespace
int main() {
    try {
        const float h = float(std::sqrt(.5));
        require(retarget_sample_intervals(1, 30, 4, 6) == 30, "Retarget sample spacing differs");
        require(retarget_sample_intervals(.0001f, 1, 1, 1) == 1,
                "Short retarget clip lost its two endpoints");
        rejects([] { retarget_sample_intervals(10, 120, 256, 1); });
        rejects([] { retarget_sample_intervals(10, 120, 1, 256); });
        rejects([] { retarget_sample_intervals(60, 120, 1, 1); });
        rejects([] { retarget_sample_intervals(1, 121, 1, 1); });
        rejects([] { retarget_sample_intervals(1, 30, 0, 1); });
        auto source = rig(2), target = rig(3);
        source.rest[1].rotation = {0, 0, h, h};
        target.rest[0].rotation = {0, 0, h, h};
        target.rest[0].translation = {10, 0, 0};
        target.rest[1].translation = {0, 3, 0};
        target.rest[2].translation = {0, 2, 0};
        AnimationRetargetSettings settings;
        settings.entries = {{source.joints[0], target.joints[0]},
                            {source.joints[1], target.joints[1]}};
        RetargetPose retarget(source, target, settings);
        auto result = retarget.sample(source.rest);
        vector(result[0].translation, {10, 0, 0});
        vector(result[1].translation, {0, 3, 0});
        vector(root_rotate(quaternion(result[0]), {1, 0, 0}), {0, 1, 0});
        vector(root_rotate(quaternion(result[1]), {1, 0, 0}), {1, 0, 0});
        require(result[2].translation == target.rest[2].translation &&
                    result[2].rotation == target.rest[2].rotation,
                "Unmapped local rest changed");
        auto animated = source.rest;
        animated[0].rotation = {0, h, 0, h};
        animated[0].translation = {0, 0, 2};
        result = retarget.sample(animated);
        vector(root_rotate(quaternion(result[0]), {0, 1, 0}), {0, 0, 1});
        vector(root_rotate(quaternion(result[1]), {1, 0, 0}), {1, 0, 0});
        vector(result[0].translation, {10, 0, 0});
        vector(result[1].translation, {0, 3, 0});
        animated[1].rotation = {0, 0, 1, 0};
        result = retarget.sample(animated);
        vector(root_rotate(quaternion(result[1]), {1, 0, 0}), {0, 1, 0});
        settings.root_translation = true;
        settings.translation_scale = 3;
        RetargetPose moving(source, target, settings);
        result = moving.sample(animated);
        vector(result[0].translation, {10, 0, 6});
        settings.alignment = {0, 0, std::sqrt(.5), std::sqrt(.5)};
        RetargetPose aligned(source, target, settings);
        result = aligned.sample(animated);
        vector(root_rotate(quaternion(result[0]), {1, 0, 0}), {0, 0, -1});
        vector(result[0].translation, {10, 0, 6});
        // A complete identity map reproduces source local rotation, independently
        // of the selected rest orientations, at both parent and child levels.
        settings = {};
        settings.entries = {{source.joints[0], source.joints[0]},
                            {source.joints[1], source.joints[1]}};
        result = RetargetPose(source, source, settings).sample(animated);
        for (unsigned i = 0; i < 2; ++i)
            vector(root_rotate(quaternion(result[i]), {1, 0, 0}),
                   root_rotate(quaternion(animated[i]), {1, 0, 0}));
        auto invalid = source;
        invalid.parents[1] = 1;
        rejects([&] { RetargetPose(invalid, source, settings); });
        invalid = source;
        invalid.rest[0].scale[0] = -1;
        rejects([&] { RetargetPose(invalid, source, settings); });
        invalid = source;
        invalid.joints[1] = invalid.joints[0];
        rejects([&] { RetargetPose(invalid, source, settings); });
        auto wrong = settings;
        wrong.entries[1].source = AssetId::generate();
        rejects([&] { RetargetPose(source, source, wrong); });
        wrong = settings;
        wrong.entries.erase(wrong.entries.begin());
        rejects([&] { RetargetPose(source, source, wrong); });
        wrong = settings;
        wrong.entries[1].target = wrong.entries[0].target;
        rejects([&] { RetargetPose(source, source, wrong); });
        wrong = settings;
        wrong.alignment = {0, 0, 0, 0};
        rejects([&] { RetargetPose(source, source, wrong); });
        rejects([&] { retarget.sample({}); });
        auto bad_pose = animated;
        bad_pose[0].translation[1] = std::numeric_limits<float>::infinity();
        rejects([&] { retarget.sample(bad_pose); });
        bad_pose = animated;
        bad_pose[0].scale[0] = 2;
        rejects([&] { retarget.sample(bad_pose); });
        std::stop_source cancelled;
        cancelled.request_stop();
        rejects([&] { retarget.sample(animated, cancelled.get_token()); });

        auto document = AnimationRetargetSource::create(AssetId::generate());
        document.validate();
        rejects([&] { (void)document.settings(); });
        document.document["source_skeleton"] = AssetId::generate();
        document.document["source_clip"] = AssetId::generate();
        document.document["target_skeleton"] = AssetId::generate();
        document.document["entries"] = {
            {{"source", source.joints[0]}, {"target", target.joints[0]}}};
        const auto text = document.document.dump();
        require(AnimationRetargetSource::parse(std::as_bytes(std::span(text)))
                        .settings()
                        .entries.size() == 1,
                "Retarget source round trip lost mapping");
        document.document["sampling_rate"] = -1;
        rejects([&] { document.validate(); });
        document.document["sampling_rate"] = 30;
        document.document["entries"].push_back(document.document["entries"][0]);
        rejects([&] { document.validate(); });

        SampledAnimation samples{{0, .5f, 1}, {target.rest, target.rest, target.rest}};
        samples.poses[1] = moving.sample(animated);
        const auto bytes = build_sampled_animation(samples);
        Clip clip(bytes);
        require(clip.info().tracks == 3 && clip.info().duration == 1 && clip.info().unit_scales,
                "Built target archive does not match admitted samples");
        // This owned unit-scale fixture ends with its compressed scale values.
        // Change an interior actual-joint key, preserving every archive control
        // and endpoint. Admission must see it even when a bake samples endpoints.
        auto scaled = bytes;
        auto first_scale = scaled.size();
        while (first_scale >= 2 && scaled[first_scale - 2] == std::byte{0} &&
               scaled[first_scale - 1] == std::byte{0x3c})
            first_scale -= 2;
        require(scaled.size() - first_scale >= 5 * 6,
                "Owned compressed unit-scale fixture layout differs");
        scaled[first_scale + 4 * 6 + 1] = std::byte{0x40}; // Half-float 2, interior key of joint0.
        Clip varying(scaled);
        require(!varying.info().unit_scales,
                "Animation admission missed a nonunit interior scale key");
        rejects([&] { build_sampled_animation(samples, cancelled.get_token()); });
        samples.times[1] = 1;
        rejects([&] { build_sampled_animation(samples); });
        samples.times[1] = .5f;
        samples.poses[1].pop_back();
        rejects([&] { build_sampled_animation(samples); });
        std::cout << "Retarget rest/motion/alignment/proportions/identity/ancestry/source/bounds/"
                     "cancellation and exact native archive admission passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
