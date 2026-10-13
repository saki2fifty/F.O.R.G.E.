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
} // namespace
int main() {
    try {
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
