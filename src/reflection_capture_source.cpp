#include "reflection_capture_source.hpp"
#include <algorithm>
#include <array>
#include <cstdint>
#include <stdexcept>
namespace forge {
namespace {
void check(bool ok, const char* message) {
    if (!ok)
        throw std::runtime_error(message);
}
std::array<std::byte, 148> capture_header(unsigned size) {
    std::array<std::byte, 148> bytes{};
    const auto field = [&](std::size_t offset, std::uint32_t value) {
        for (unsigned i = 0; i < 4; ++i)
            bytes.at(offset + i) = std::byte((value >> (i * 8)) & 255);
    };
    // DDS signature,124-byte base header and32-byte pixel-format record.
    field(0, 0x20534444);
    field(4, 124);
    field(8, 0x100f); // caps,height,width,pitch,pixel-format
    field(12, size);
    field(16, size);
    field(20, std::uint32_t(size * 8));
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
    return bytes;
}
} // namespace
unsigned reflection_capture_source_size(std::span<const std::byte> bytes) {
    check(bytes.size() >= 148, "Existing map has no complete capture source header");
    unsigned size = 0;
    for (unsigned i = 0; i < 4; ++i)
        size |= std::to_integer<unsigned>(bytes[16 + i]) << (8 * i);
    check(size >= 16 && size <= 1024, "Existing capture face size must be16..1024");
    const auto expected = capture_header(size);
    check(std::equal(expected.begin(), expected.end(), bytes.begin()),
          "Update requires an existing single-mip opaque RGBA16F capture DDS cube");
    return size;
}
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
    const auto header = capture_header(cube.width);
    std::vector<std::byte> bytes(header.begin(), header.end());
    bytes.reserve(148 + cube.byte_size());
    for (const auto& face : cube.subresources)
        bytes.insert(bytes.end(), face.begin(), face.end());
    return bytes;
}
} // namespace forge
