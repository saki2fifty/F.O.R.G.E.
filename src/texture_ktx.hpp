#pragma once
#include <forge/texture_asset.hpp>
#include <stop_token>

namespace forge::asset_detail {
enum class BasisEncoding { Etc1s, Uastc };
enum class BasisTarget { DesktopBc, Rgba8 };
// Private worker boundary. Source metadata determines transfer function; semantic
// checks reject contradictions. Supplied mip levels and dimensions are retained.
TextureData import_texture_ktx2(std::span<const std::byte> bytes, TextureSemantic semantic,
                                BasisTarget target = BasisTarget::DesktopBc,
                                bool gltf_basisu = false, TextureLimits limits = {},
                                std::stop_token stop = {});
TextureData import_texture_ktx1(std::span<const std::byte> bytes, TextureSemantic semantic,
                                TextureLimits limits = {}, std::stop_token stop = {});
// Already prepared RGBA8 mip chains; codecs do not generate/filter mips here.
// Repeatable within the recorded platform/toolchain/options profile only.
std::vector<std::byte> encode_texture_basis(const TextureData& texture, BasisEncoding encoding,
                                            TextureLimits limits = {}, std::stop_token stop = {});
} // namespace forge::asset_detail
