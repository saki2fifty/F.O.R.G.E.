#pragma once
#include <cstdint>
#include <filesystem>
#include <span>
namespace forge {
// Detached display pixels; no device, Scene, catalog or project-writer ownership.
void save_view_png(const std::filesystem::path&, unsigned width, unsigned height,
                   std::span<const std::uint8_t> rgba);
} // namespace forge
