#pragma once
#include "tile_set_values.hpp"
#include <memory>
#include <span>

namespace forge {
inline constexpr std::array<std::byte, 8> tile_set_cooked_magic{
    std::byte{'F'}, std::byte{'T'}, std::byte{'I'}, std::byte{'L'},
    std::byte{'E'}, std::byte{'S'}, std::byte{'E'}, std::byte{'T'}};
inline constexpr std::size_t tile_set_cooked_metadata_limit = 2 * 1024 * 1024;
inline constexpr std::uint32_t tile_set_cooked_version = 6;
class TileSetData {
  public:
    AssetId asset() const;
    const std::vector<TileSetImage>& images() const;
    const std::vector<TileSetDefinition>& tiles() const;
    const TileSetImage& image(std::uint64_t) const;
    const TileSetDefinition& tile(std::uint64_t) const;
    bool has_autotiles() const;
    std::vector<AssetRef<MaterialAsset>> materials() const;
    void validate() const;
    std::size_t resident_bytes() const;

  private:
    AssetId asset_;
    std::shared_ptr<const TileSetRows> rows_;
    friend TileSetData decode_cooked_tile_set(std::span<const std::byte>, AssetId, std::uint32_t);
};
TileSetData decode_cooked_tile_set(std::span<const std::byte>, AssetId expected,
                                   std::uint32_t expected_version = 0);
} // namespace forge
