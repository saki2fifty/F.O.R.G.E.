#include "reflection_capture_source.hpp"
#include <cstdint>
#include <stdexcept>
namespace forge {
namespace {
void check(bool ok, const char* message) {
    if (!ok)
        throw std::runtime_error(message);
}
} // namespace
std::vector<std::byte> encode_reflection_capture_source(const TextureData& cube,
                                                        TextureLimits limits) {
    validate_texture(cube, limits);
    check(cube.dimension == TextureDimension::Cube && cube.layers == 1 && cube.mips == 1 &&
              cube.format == TextureFormat::RGBA16Float &&
              cube.semantic == TextureSemantic::HdrColor && cube.alpha == TextureAlpha::Opaque,
          "Capture source requires one opaque HDR half-float cube");
    check(limits.bytes >= 148 && cube.byte_size() <= limits.bytes - 148,
          "Capture source header and pixels exceed the complete byte budget");
    for (const auto& face : cube.subresources)
        for (std::size_t at = 0; at < face.size(); at += 2) {
            const unsigned half = std::to_integer<unsigned>(face[at]) |
                                  (std::to_integer<unsigned>(face[at + 1]) << 8);
            check((half & 0x7c00) != 0x7c00 && (!(half & 0x8000) || (half & 0x7fff) == 0),
                  "Capture pixels must be finite and nonnegative");
            if (at % 8 == 6)
                check(half == 0x3c00, "Opaque capture alpha must be one");
        }
    std::vector<std::byte> bytes(148);
    const auto field = [&](std::size_t offset, std::uint32_t value) {
        for (unsigned i = 0; i < 4; ++i)
            bytes.at(offset + i) = std::byte((value >> (i * 8)) & 255);
    };
    // DDS signature,124-byte base header and32-byte pixel-format record.
    field(0, 0x20534444);
    field(4, 124);
    field(8, 0x100f); // caps,height,width,pitch,pixel-format
    field(12, cube.height);
    field(16, cube.width);
    field(20, std::uint32_t(texture_layout(cube, 0).row_bytes));
    field(28, 1);
    field(76, 32);
    field(80, 4);          // FOURCC
    field(84, 0x30315844); // DX10
    field(108, 0x1008);    // texture,complex
    field(112, 0xfe00);    // cube and all six faces
    field(128, 10);        // DXGI R16G16B16A16_FLOAT
    field(132, 3);         // 2D resource
    field(136, 4);         // cube
    field(140, 1);         // number of cubes, not faces
    field(144, 3);         // opaque alpha
    bytes.reserve(148 + cube.byte_size());
    for (const auto& face : cube.subresources)
        bytes.insert(bytes.end(), face.begin(), face.end());
    return bytes;
}
} // namespace forge
