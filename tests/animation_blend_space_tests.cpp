#include <algorithm>
#include <cmath>
#include <forge/animation_blend_space.hpp>
#include <iostream>
#include <limits>
using namespace forge;
namespace {
void require(bool value, const char* why) {
    if (!value)
        throw std::runtime_error(why);
}
template <class F> void rejects(F run) {
    bool failed = false;
    try {
        run();
    } catch (const std::exception&) {
        failed = true;
    }
    require(failed, "Invalid Blend Space admitted");
}
AnimationBlendSpaceSource plane_source(std::vector<std::array<double, 2>> points) {
    auto source = AnimationBlendSpaceSource::create(AssetId::generate(), 2);
    source.document["skeleton"] = AssetId::generate();
    for (const auto& point : points)
        source.document["samples"].push_back({{"id", AssetId::generate()},
                                              {"clip", AssetId::generate()},
                                              {"position", {{"x", point[0]}, {"y", point[1]}}}});
    return source;
}
void reconstruct(const AnimationBlendSpace& space, double x, double y, double expected_x,
                 double expected_y) {
    const auto selected = space.select(x, y);
    require(selected.count >= 1 && selected.count <= 3, "2D selection exceeded three layers");
    double sum = 0, result_x = 0, result_y = 0;
    for (std::size_t i = 0; i < selected.count; ++i) {
        require(selected.samples[i] < space.samples.size() && selected.weights[i] > 0 &&
                    selected.weights[i] <= 1,
                "2D selection produced an invalid index or influence");
        sum += selected.weights[i];
        result_x += space.samples[selected.samples[i]].position * selected.weights[i];
        result_y += space.samples[selected.samples[i]].position_y * selected.weights[i];
    }
    require(std::abs(sum - 1) < 1e-12 && std::abs(result_x - expected_x) < 1e-9 &&
                std::abs(result_y - expected_y) < 1e-9,
            "2D weights did not reconstruct the requested or closest boundary point");
}
void plane_tests() {
    auto source = plane_source({{0, 0}, {2, 0}, {0, 2}});
    const auto cooked = source.cook();
    const auto space = decode_animation_blend_space(cooked, source.asset());
    require(space.dimensions == 2 && space.resident_bytes() > sizeof(space),
            "2D cooked admission lost its axes or resident budget");
    reconstruct(space, .5, .5, .5, .5);
    require(space.select(.5, .5).count == 3, "Interior point did not blend three samples");
    reconstruct(space, 3, 3, 1, 1);
    reconstruct(space, -1, .5, 0, .5);
    reconstruct(space, -1, -1, 0, 0);
    for (const auto& sample : space.samples) {
        reconstruct(space, sample.position, sample.position_y, sample.position, sample.position_y);
        require(space.select(sample.position, sample.position_y).count == 1,
                "Exact 2D vertex sampled an unnecessary layer");
    }
    rejects([&] { (void)space.select(.5); });
    rejects([&] { (void)space.select(0, std::numeric_limits<double>::infinity()); });
    std::reverse(source.document["samples"].begin(), source.document["samples"].end());
    require(source.cook() == cooked && source.compiled().triangles == space.triangles,
            "2D source order changed immutable output or prepared selection");
    const auto line = plane_source({{0, 0}, {1, 1}, {2, 2}}).compiled();
    reconstruct(line, .5, 1.5, 1, 1);
    reconstruct(line, 1, 3, 2, 2);
    reconstruct(plane_source({{1, 3}}).compiled(), -5, 7, 1, 3);
    reconstruct(plane_source({{0, 0}, {2, 0}}).compiled(), 1, 3, 1, 0);
    std::vector<std::array<double, 2>> grid;
    for (int x = 0; x < 8; ++x)
        for (int y = 0; y < 8; ++y)
            grid.push_back({double(x), double(y)});
    auto grid_source = plane_source(grid);
    const auto grid_cooked = grid_source.cook();
    const auto grid_space = grid_source.compiled();
    for (int x = -2; x <= 30; ++x)
        for (int y = -2; y <= 30; ++y)
            reconstruct(grid_space, x / 4., y / 4., std::clamp(x / 4., 0., 7.),
                        std::clamp(y / 4., 0., 7.));
    std::rotate(grid_source.document["samples"].begin(),
                grid_source.document["samples"].begin() + 17,
                grid_source.document["samples"].end());
    require(grid_source.cook() == grid_cooked,
            "Cocircular/grid source order changed canonical2D output");
    auto invalid = source;
    invalid.document["samples"][0]["position"] = 0;
    rejects([&] { invalid.validate(); });
    invalid = source;
    invalid.document["samples"][0]["position"] = invalid.document["samples"][1]["position"];
    rejects([&] { invalid.validate(); });
    invalid = source;
    invalid.document["samples"][0]["position"].erase("y");
    rejects([&] { invalid.validate(); });
    rejects([&] { (void)AnimationBlendSpaceSource::create(AssetId::generate(), 3); });
    auto wrapper = nlohmann::json::parse(cooked);
    wrapper["version"] = 1;
    const auto mixed = wrapper.dump();
    rejects(
        [&] { (void)AnimationBlendSpaceSource::parse_cooked(std::as_bytes(std::span(mixed))); });
}
} // namespace
int main() {
    try {
        plane_tests();
        const auto id = AssetId::generate(), skeleton = AssetId::generate();
        auto source = AnimationBlendSpaceSource::create(id);
        source.validate();
        rejects([&] { (void)source.cook(); });
        source.document["skeleton"] = skeleton;
        const auto a = AssetId::generate(), b = AssetId::generate(), c = AssetId::generate();
        source.document["samples"] = {{{"id", AssetId::generate()}, {"position", 2}, {"clip", c}},
                                      {{"id", AssetId::generate()}, {"position", 0}, {"clip", a}},
                                      {{"id", AssetId::generate()}, {"position", 1}, {"clip", b}}};
        source.document["cycle_seconds"] = 2.5;
        const auto cooked = source.cook();
        const auto space = decode_animation_blend_space(cooked, id);
        const auto revised_wrapper = source.cook(2);
        const auto revised_space = decode_animation_blend_space(revised_wrapper, id, 2);
        require(revised_space.dimensions == 1 && revised_space.samples[0].clip.id == a &&
                    revised_space.select(.5).weight == .5,
                "Recipe2 wrapper changed a legacy1D source's dimension or interpolation");
        rejects([&] { (void)decode_animation_blend_space(revised_wrapper, id, 1); });
        require(space.skeleton == skeleton && space.cycle_seconds == 2.5 &&
                    space.samples[0].clip.id == a && space.samples[2].clip.id == c,
                "Blend Space cook lost identity, timing or order");
        for (const double p : {-5., 0., .25, 1., 1.75, 2., 5.}) {
            const auto pair = space.select(p);
            const double reconstructed = space.samples[pair.first].position * (1 - pair.weight) +
                                         space.samples[pair.second].position * pair.weight;
            require(std::abs(reconstructed - std::clamp(p, 0., 2.)) < 1e-12,
                    "Neighbor weights do not reconstruct the clamped position");
            require(pair.first == pair.second || pair.second == pair.first + 1,
                    "Blend Space selected nonadjacent samples");
        }
        std::reverse(source.document["samples"].begin(), source.document["samples"].end());
        require(source.cook() == cooked, "Source order changed deterministic Blend Space output");
        rejects([&] { (void)space.select(std::numeric_limits<double>::infinity()); });
        rejects([&] { (void)decode_animation_blend_space(cooked, AssetId::generate()); });
        auto invalid = source;
        invalid.document["samples"][0]["position"] = invalid.document["samples"][1]["position"];
        rejects([&] { invalid.validate(); });
        invalid = source;
        invalid.document["samples"][0]["id"] = invalid.document["samples"][1]["id"];
        rejects([&] { invalid.validate(); });
        invalid = source;
        invalid.document["samples"][0]["clip"] = nullptr;
        invalid.validate();
        rejects([&] { (void)invalid.cook(); });
        for (const auto value :
             {nlohmann::json(0), nlohmann::json(-1), nlohmann::json(true), nlohmann::json(3601)}) {
            invalid = source;
            invalid.document["cycle_seconds"] = value;
            rejects([&] { invalid.validate(); });
        }
        source.document["samples"].erase(source.document["samples"].begin() + 1,
                                         source.document["samples"].end());
        const auto single = source.compiled().select(-100);
        require(single.first == 0 && single.second == 0 && single.weight == 0,
                "Single-sample space did not clamp");
        std::cout << "Blend Space durable samples, neighbor interpolation, endpoint clamp, timing, "
                     "deterministic cook and invalid draft rejection passed\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
