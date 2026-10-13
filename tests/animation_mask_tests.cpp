#include <algorithm>
#include <array>
#include <cmath>
#include <forge/animation_mask.hpp>
#include <iostream>
using namespace forge;
namespace {
void require(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
template <class F> void rejects(F run, const char* message) {
    bool failed = false;
    try {
        run();
    } catch (const std::exception&) {
        failed = true;
    }
    require(failed, message);
}
void equal(std::span<const float> actual, std::initializer_list<float> expected) {
    require(actual.size() == expected.size(), "Mask joint count differs");
    std::size_t index = 0;
    for (const auto value : expected)
        require(std::abs(actual[index++] - value) < 1e-6, "Mask ancestry influence differs");
}
} // namespace
int main() {
    try {
        const auto id = AssetId::generate(), skeleton = AssetId::generate();
        const std::array<AssetId, 5> joints{AssetId::generate(), AssetId::generate(),
                                            AssetId::generate(), AssetId::generate(),
                                            AssetId::generate()};
        const std::array<std::int16_t, 5> parents{-1, 0, 1, 1, 0};
        auto source = AnimationMaskSource::create(id);
        source.validate();
        rejects([&] { (void)source.cook(); }, "Incomplete mask was published");
        source.document["skeleton"] = skeleton;
        source.document["default_weight"] = .2;
        source.document["entries"] = {
            {{"joint", joints[0]}, {"weight", .8}, {"descendants", true}},
            {{"joint", joints[1]}, {"weight", .5}, {"descendants", false}},
            {{"joint", joints[3]}, {"weight", .25}, {"descendants", true}}};
        const auto bytes = source.cook();
        const auto mask = decode_animation_mask(bytes, id);
        equal(mask.weights(skeleton, joints, parents), {.8f, .5f, .8f, .25f, .8f});
        auto reordered = source;
        std::reverse(reordered.document["entries"].begin(), reordered.document["entries"].end());
        require(reordered.cook() == bytes, "Mask row order changed deterministic cooked output");
        // Source nodes reordered and renamed retain their durable references.
        const std::array<AssetId, 5> revised{joints[0], joints[4], joints[1], joints[3], joints[2]};
        const std::array<std::int16_t, 5> revised_parents{-1, 0, 0, 2, 2};
        equal(mask.weights(skeleton, revised, revised_parents), {.8f, .8f, .5f, .25f, .8f});
        rejects([&] { (void)decode_animation_mask(bytes, AssetId::generate()); },
                "Cooked mask lost identity");
        rejects([&] { (void)mask.weights(AssetId::generate(), joints, parents); },
                "Foreign mask skeleton admitted");
        auto invalid = source;
        invalid.document["entries"].push_back(invalid.document["entries"][0]);
        rejects([&] { invalid.validate(); }, "Duplicate mask joint admitted");
        invalid = source;
        invalid.document["entries"][0]["weight"] = 1.01;
        rejects([&] { invalid.validate(); }, "Extrapolated bone influence admitted");
        invalid.document["entries"][0]["weight"] = true;
        rejects([&] { invalid.validate(); }, "Boolean bone influence admitted");
        invalid = source;
        invalid.document["entries"][0]["joint"] = AssetId::generate();
        const auto foreign = invalid.compiled();
        rejects([&] { (void)foreign.weights(skeleton, joints, parents); },
                "Foreign mask joint admitted");
        auto missing = joints;
        missing[3] = AssetId::generate();
        rejects([&] { (void)mask.weights(skeleton, missing, parents); },
                "Removed mask joint silently rebound");
        auto cycle = parents;
        cycle[2] = 3;
        rejects([&] { (void)mask.weights(skeleton, joints, cycle); },
                "Invalid mask ancestry admitted");
        auto unbound = mask;
        unbound.entries.clear();
        equal(unbound.weights(skeleton, joints, parents), {.2f, .2f, .2f, .2f, .2f});
        const std::string too_big(animation_mask_byte_limit + 1, ' ');
        rejects([&] { (void)AnimationMaskSource::parse(std::as_bytes(std::span(too_big))); },
                "Oversized mask source admitted");
        std::cout << "Mask identity, nearest-ancestor influence, joint-only scope, deterministic "
                     "cooking and rejection passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
