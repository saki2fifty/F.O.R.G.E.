#pragma once
#include "cooked_envelope.hpp"
#include "tile_set_cook.hpp"
#include <iostream>

namespace forge::test_tile_neighbors_source {
using Json = nlohmann::json;
inline void check(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
template <class F> void reject(F&& function) {
    try {
        function();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error("Invalid neighbor source was accepted");
}
inline TileSetSource fixture() {
    auto source = TileSetSource::create(AssetId::generate());
    source.document["next_id"] = 5;
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
        {{"id", 2}, {"name", "Root"}, {"image", 1}, {"frame", 0}},
        {{"id", 3}, {"name", "Gap"}, {"image", 1}, {"frame", 1}},
        {{"id", 4}, {"name", "Animated variant"}, {"image", 1}, {"frame", 2}}};
    return source;
}
inline Json profile() {
    return {{"enabled", true},
            {"rules",
             {{{"mask", 1}, {"same", 1}, {"tile", 3}}, {{"mask", 1}, {"same", 0}, {"tile", 4}}}}};
}
inline void exercise() {
    auto source = fixture();
    source.set_collision(3, {{"kind", "none"}});
    source.set_animation(
        4, {{"enabled", true},
            {"frames", {{{"frame", 1}, {"seconds", .125}}, {{"frame", 3}, {"seconds", .125}}}}});
    source.set_autotile(2, profile());
    check(source.document.at("version") == 5 && source.document.at("next_id") == 5,
          "Neighbor source promotion changed version/allocation");
    source.document["tiles"][0]["autotile"]["opaque"] = "preserved";
    source.document["tiles"][0]["autotile"]["rules"][0]["note"] = "first position";
    source.set_autotile(2, profile());
    check(source.document["tiles"][0]["autotile"].at("opaque") == "preserved" &&
              source.document["tiles"][0]["autotile"]["rules"][0].at("note") == "first position",
          "Typed neighbor edit lost unrecognized siblings");
    const auto bytes = cook_tile_set(source);
    auto data = decode_cooked_tile_set(bytes, source.asset(), 5);
    check(data.has_autotiles() && data.tile(2).autotile && data.tile(4).animation &&
              data.tile(3).collision.kind == TileCollisionKind::None,
          "Cooked neighbor profile lost animation/collision interoperability");
    const auto& config = *data.tile(2).autotile;
    for (unsigned mask = 0; mask < 256; ++mask)
        check(tile_detail::select_tile_neighbor(config.rules, config.lookup, mask, 2) ==
                  (mask & 1 ? 3 : 4),
              "Cooked rule order/table differs");
    const auto before = source.document;
    for (unsigned kind = 0; kind < 7; ++kind) {
        auto bad = profile();
        if (kind == 0)
            bad["rules"] = Json::array();
        if (kind == 1)
            bad["rules"][0]["same"] = 2;
        if (kind == 2)
            bad["rules"][0]["tile"] = 0;
        if (kind == 3)
            bad["rules"][0]["tile"] = 1;
        if (kind == 4)
            bad["rules"][0]["mask"] = 256;
        if (kind == 5)
            bad["rules"][0]["same"] = true;
        if (kind == 6)
            bad["rules"] = std::vector<Json>(65, bad["rules"][0]);
        reject([&] { source.set_autotile(2, bad); });
        check(source.document == before, "Rejected rule changed the complete source");
    }
    reject([&] {
        source.set_autotile(
            3, {{"enabled", true}, {"rules", {{{"mask", 0}, {"same", 0}, {"tile", 3}}}}});
    });
    check(source.document == before, "Enabling a referenced variant changed source");
    auto self = source;
    self.set_autotile(2, {{"enabled", true}, {"rules", {{{"mask", 0}, {"same", 0}, {"tile", 2}}}}});
    check(
        decode_cooked_tile_set(cook_tile_set(self), self.asset()).tile(2).autotile->rules[0].tile ==
            2,
        "Self/base variant was rejected");
    auto disabled = source;
    disabled.set_autotile(2, {{"enabled", false}, {"rules", Json::array()}});
    check(!decode_cooked_tile_set(cook_tile_set(disabled), disabled.asset()).has_autotiles(),
          "Disabled empty rules remained active");
    auto high = source;
    high.document["next_id"] = UINT64_MAX;
    high.document["tiles"][2]["id"] = UINT64_MAX - 1;
    high.document["tiles"][0]["autotile"]["rules"][1]["tile"] = UINT64_MAX - 1;
    const auto high_data = decode_cooked_tile_set(cook_tile_set(high), high.asset());
    check(high_data.tile(2).autotile->rules[1].tile == UINT64_MAX - 1,
          "Neighbor local ID passed through a floating point conversion");
    for (unsigned version = 1; version <= 4; ++version) {
        auto legacy = fixture();
        legacy.document["version"] = version;
        legacy.document["tiles"][1]["autotile"] = {{"opaque", "legacy"}};
        const auto original = legacy.document;
        check(!decode_cooked_tile_set(cook_tile_set(legacy), legacy.asset()).has_autotiles(),
              "Legacy neighbor extension was interpreted");
        reject([&] { legacy.set_autotile(2, profile()); });
        check(legacy.document == original, "Legacy conflict lost source/IDs");
    }
    const auto envelope = asset_detail::decode_envelope(bytes, tile_set_cooked_magic,
                                                        tile_set_cooked_metadata_limit, 0, 5);
    for (unsigned version = 1; version <= 4; ++version) {
        auto metadata = envelope.metadata;
        for (auto& row : metadata["tiles"]) {
            row.erase("autotile");
            if (version < 4)
                row.erase("animation");
            if (version < 2)
                row.erase("collision");
        }
        const auto old = asset_detail::encode_envelope(metadata, {}, tile_set_cooked_magic,
                                                       tile_set_cooked_metadata_limit, version);
        check(!decode_cooked_tile_set(old, source.asset(), version).has_autotiles(),
              "Old cooked read inferred neighbor rules");
    }
    auto metadata = envelope.metadata;
    metadata["tiles"][0]["autotile"]["lookup"] = Json::array();
    reject([&] {
        (void)decode_cooked_tile_set(
            asset_detail::encode_envelope(metadata, {}, tile_set_cooked_magic,
                                          tile_set_cooked_metadata_limit, 5),
            source.asset());
    });
    std::cout << "Source5/cooked5 neighbor rules, old1..4reads, guards, unknowns, terminal "
                 "variants and64bit IDs PASS\n";
}
} // namespace forge::test_tile_neighbors_source
