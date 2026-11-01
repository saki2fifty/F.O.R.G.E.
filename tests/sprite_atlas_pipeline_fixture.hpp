#pragma once
#include "asset_import_service.hpp"
#include "texture_authoring.hpp"
#include <array>
#include <forge/scene.hpp>
namespace forge::test_sprite_atlas_io {
inline void image(const std::filesystem::path& path, unsigned width, unsigned height,
                  std::array<unsigned char, 3> rgb) {
    std::vector<std::byte> bytes(18);
    bytes[2] = std::byte{2};
    bytes[12] = std::byte(width);
    bytes[14] = std::byte(height);
    bytes[16] = std::byte{24};
    bytes[17] = std::byte{32};
    for (unsigned i = 0; i < width * height; ++i)
        bytes.insert(bytes.end(), {std::byte(rgb[2]), std::byte(rgb[1]), std::byte(rgb[0])});
    atomic_write(path, std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
}
inline AssetImportOutcome publish(AssetImportService& service, const std::filesystem::path& source,
                                  AssetId id) {
    service.submit(
        service.prepare(source, {}, id),
        [](auto& c, const auto& p, const auto&) { prepare_texture_publication(c, p); },
        [](const auto&, const auto&) {});
    if (!service.wait_idle(std::chrono::seconds(15)))
        throw std::runtime_error("Atlas pipeline timed out");
    auto outcomes = service.poll();
    if (outcomes.size() != 1)
        throw std::runtime_error("Atlas pipeline receipt missing");
    return std::move(outcomes.front());
}
} // namespace forge::test_sprite_atlas_io
