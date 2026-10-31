#include "cooked_envelope.hpp"
#include "tile_animation.hpp"
#include "tile_set_cook.hpp"
#include <array>
#include <iostream>
#include <limits>
#include <random>
#include <vector>
using namespace forge;
using namespace forge::tile_detail;
void check(bool value, const char* why) {
    if (!value)
        throw std::runtime_error(why);
}
template <class F> void rejects(F action) {
    try {
        action();
    } catch (const std::invalid_argument&) {
        return;
    }
    throw std::runtime_error("Invalid tile sequence/time was accepted");
}
template <class F> void rejects_source(F action) {
    try {
        action();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error("Invalid tile animation source/cook was accepted");
}
void sources() {
    using Json = nlohmann::json;
    auto source = TileSetSource::create(AssetId::generate());
    source.document["next_id"] = 3;
    source.document["opaque"] = "ROOT";
    source.document["images"] = {{{"id", 1},
                                  {"name", "Sheet"},
                                  {"material", AssetId::generate()},
                                  {"grid",
                                   {{"columns", 4},
                                    {"rows", 1},
                                    {"margin_x", 0},
                                    {"margin_y", 0},
                                    {"spacing_x", 0},
                                    {"spacing_y", 0}}}}};
    source.document["tiles"] = {
        {{"id", 2}, {"name", "Animated"}, {"image", 1}, {"frame", 2}, {"opaque", "TILE"}}};
    const auto legacy = source;
    auto bytes = cook_tile_set(source);
    const auto envelope = asset_detail::decode_envelope(
        bytes, tile_set_cooked_magic, tile_set_cooked_metadata_limit, 0, tile_set_cooked_version);
    for (unsigned version = 1; version <= 3; ++version) {
        auto metadata = envelope.metadata;
        if (version == 1)
            metadata["tiles"][0].erase("collision");
        const auto old = decode_cooked_tile_set(
            asset_detail::encode_envelope(metadata, {}, tile_set_cooked_magic,
                                          tile_set_cooked_metadata_limit, version),
            source.asset(), version);
        check(old.tile(2).frame == 2 && !old.tile(2).animation,
              "Old cooked1/2/3 acquired animation");
    }
    Json profile{{"enabled", true},
                 {"frames",
                  {{{"frame", 0}, {"seconds", .125}},
                   {{"frame", 3}, {"seconds", .25}},
                   {{"frame", 1}, {"seconds", .5}}}}};
    source.set_animation(2, profile);
    source.document["tiles"][0]["animation"]["opaque"] = "PROFILE";
    source.document["tiles"][0]["animation"]["frames"][0]["opaque"] = "FRAME";
    const auto expected = source.document;
    bytes = cook_tile_set(source);
    const auto data = decode_cooked_tile_set(bytes, source.asset(), tile_set_cooked_version);
    const auto& animation = *data.tile(2).animation;
    check(source.document == expected && source.document.at("version") == 4 &&
              source.document.at("next_id") == 3 && animation.enabled &&
              animation.frames.size() == 3 && animation.duration == .875 &&
              tile_animation_frame_at(animation.frames, animation.duration, 2, .125) == 3,
          "Source4/cooked4 values, identity or frame boundary differs");
    const auto copy = data;
    check(copy.tile(2).animation == data.tile(2).animation,
          "Immutable animation copy duplicated its frame data");
    const std::string encoded(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    check(encoded.find("opaque") == std::string::npos,
          "Cook retained animation/source extension fields");
    profile["enabled"] = false;
    source.set_animation(2, profile);
    check(source.document["tiles"][0]["animation"]["opaque"] == "PROFILE" &&
              source.document["tiles"][0]["animation"]["frames"][0]["opaque"] == "FRAME" &&
              source.document["tiles"][0]["opaque"] == "TILE",
          "Animation edit discarded retained source extensions");
    const auto before = source.document;
    for (unsigned scenario = 0; scenario < 8; ++scenario) {
        auto bad = profile;
        bad["enabled"] = true;
        if (scenario == 0)
            bad["frames"] = Json::array();
        if (scenario == 1)
            bad["frames"][0]["seconds"] = 0;
        if (scenario == 2)
            bad["frames"][0]["seconds"] = 601;
        if (scenario == 3)
            bad["frames"][0]["frame"] = 4;
        if (scenario == 4)
            bad["frames"][0]["frame"] = 1.;
        if (scenario == 5)
            bad["enabled"] = 1;
        if (scenario == 6)
            bad["frames"] = std::vector<Json>(65, profile["frames"][0]);
        if (scenario == 7)
            bad["frames"][0]["seconds"] = std::numeric_limits<double>::quiet_NaN();
        rejects_source([&] { source.set_animation(2, bad); });
        check(source.document == before, "Invalid animation changed source values");
    }
    for (unsigned version = 1; version <= 3; ++version) {
        auto opaque = legacy;
        opaque.document["version"] = version;
        opaque.document["tiles"][0]["animation"] = {{"arbitrary", 17}};
        check(!opaque.values().tiles[0].animation &&
                  !decode_cooked_tile_set(cook_tile_set(opaque), opaque.asset()).tile(2).animation,
              "Legacy opaque animation acquired runtime meaning");
        const auto prior = opaque.document;
        rejects_source([&] { opaque.set_animation(2, profile); });
        check(opaque.document == prior, "Legacy animation conflict changed source");
    }
    auto downgrade = source;
    downgrade.document["version"] = 3;
    rejects_source([&] { source.validate_upgrade(downgrade); });
    rejects_source([&] { (void)decode_cooked_tile_set(bytes, source.asset(), 3); });
    auto cooked =
        asset_detail::decode_envelope(bytes, tile_set_cooked_magic, tile_set_cooked_metadata_limit,
                                      0, tile_set_cooked_version)
            .metadata;
    for (unsigned scenario = 0; scenario < 3; ++scenario) {
        auto bad = cooked;
        if (scenario == 0)
            bad["tiles"][0]["animation"]["opaque"] = 1;
        if (scenario == 1)
            bad["tiles"][0]["animation"]["frames"][0]["opaque"] = 1;
        rejects_source([&] {
            (void)decode_cooked_tile_set(
                asset_detail::encode_envelope(bad, {}, tile_set_cooked_magic,
                                              tile_set_cooked_metadata_limit,
                                              scenario == 2 ? 3 : tile_set_cooked_version),
                source.asset());
        });
    }
}
int main() {
    try {
        sources();
        const std::array sequence{TileAnimationFrame{9, .125}, TileAnimationFrame{3, .25},
                                  TileAnimationFrame{7, .5}};
        const auto period = tile_animation_duration(sequence, 10);
        check(period == .875, "Wrong duration");
        for (const auto& [time, frame] : std::array<std::pair<double, unsigned>, 9>{{{0, 9},
                                                                                     {.124, 9},
                                                                                     {.125, 3},
                                                                                     {.374, 3},
                                                                                     {.375, 7},
                                                                                     {.874, 7},
                                                                                     {.875, 9},
                                                                                     {1., 3},
                                                                                     {1.25, 7}}})
            check(tile_animation_frame_at(sequence, period, 42, time) == frame,
                  "Boundary selection differs");
        check(tile_animation_frame_at({}, 0, 42, 1e300) == 42, "Static fallback changed");
        std::mt19937_64 random(200);
        unsigned selected = 0;
        for (unsigned count = 1; count <= 64; ++count) {
            std::vector<TileAnimationFrame> frames;
            std::vector<unsigned> units;
            unsigned cycle = 0;
            for (unsigned i = 0; i < count; ++i) {
                const auto ticks = 1 + unsigned(random() % 17);
                cycle += ticks;
                units.push_back(ticks);
                frames.push_back({std::uint32_t(random() % 1000), ticks / 64.});
            }
            const auto duration = tile_animation_duration(frames, 1000);
            for (unsigned sample = 0; sample < 1024; ++sample) {
                const auto tick = random() & ((UINT64_C(1) << 52) - 1);
                const auto phase = tick % cycle;
                unsigned sum = 0, expected = frames.back().frame;
                for (unsigned i = 0; i < count; ++i)
                    if ((sum += units[i]) > phase) {
                        expected = frames[i].frame;
                        break;
                    }
                check(tile_animation_frame_at(frames, duration, 42, double(tick) / 64) == expected,
                      "Selection differs from independent integer timeline");
                ++selected;
            }
        }
        for (const auto value : {-1., std::numeric_limits<double>::infinity(),
                                 std::numeric_limits<double>::quiet_NaN()})
            rejects([&] { (void)tile_animation_frame_at(sequence, period, 42, value); });
        rejects([&] { (void)tile_animation_duration({}, 10); });
        rejects([&] { (void)tile_animation_duration(sequence, 9); });
        auto invalid = std::vector<TileAnimationFrame>(65, {0, .1});
        rejects([&] { (void)tile_animation_duration(invalid, 10); });
        for (const auto duration : {0., -.1, .0001, 601., std::numeric_limits<double>::infinity(),
                                    std::numeric_limits<double>::quiet_NaN()}) {
            const std::array bad{TileAnimationFrame{0, duration}};
            rejects([&] { (void)tile_animation_duration(bad, 10); });
        }
        std::cout << selected
                  << " independent integer-timeline samples, variable durations, large times, "
                     "boundaries/static and invalid inputs PASS\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
