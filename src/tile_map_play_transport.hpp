#pragma once
#include "tile_map_play_projection.hpp"
#include <string_view>
namespace forge {
inline constexpr unsigned tile_map_play_protocol = 1;
inline bool tile_map_play_command(std::string_view command) {
    return command == "tile_map_begin" || command == "tile_map_page" || command == "tile_map_ack" ||
           command == "tile_map_cancel";
}
inline nlohmann::json tile_map_play_dispatch(TileMapPlaySource& source,
                                             const nlohmann::json& request) {
    const auto natural = [](const nlohmann::json& value) {
        if (!(value.is_number_unsigned() ||
              (value.is_number_integer() && value.get<std::int64_t>() >= 0)))
            throw std::runtime_error("Tile Play command requires an unsigned integer");
        return value.get<std::uint64_t>();
    };
    const auto world = natural(request.at("world"));
    const auto scene = request.at("scene").get<AssetId>();
    if (!world || world != source.world() || scene != source.scene())
        return {{"stale", true}};
    const auto command = request.at("command").get<std::string>();
    if (command == "tile_map_begin")
        return source.begin(request.at("entity").get<EntityId>(), natural(request.at("generation")),
                            natural(request.at("revision")), natural(request.at("base_generation")),
                            natural(request.at("base_revision")));
    const auto token = natural(request.at("token"));
    if (command == "tile_map_page") {
        const auto cursor = natural(request.at("cursor"));
        if (cursor > tile_map_chunk_limit * 2)
            throw std::runtime_error("Tile Play cursor exceeds its profile");
        return source.page(token, std::size_t(cursor));
    }
    if (command == "tile_map_ack")
        return {{"accepted", source.acknowledge(token)}};
    if (command == "tile_map_cancel")
        return {{"accepted", source.cancel(token)}};
    throw std::runtime_error("Unknown Tile Play command");
}
} // namespace forge
