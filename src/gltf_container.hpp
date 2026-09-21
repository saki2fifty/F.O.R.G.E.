#pragma once
#include <forge/project_paths.hpp>
#include <optional>
#include <span>
#include <vector>
namespace forge::gltf_detail {
struct ContainerChunk {
    std::uint32_t kind;
    std::span<const std::byte> bytes;
};
struct ContainerView {
    bool binary = false;
    std::span<const std::byte> json;
    std::optional<std::span<const std::byte>> bin;
    std::vector<ContainerChunk> chunks;
};
// Non-owning bounded container views; caller retains the exact input bytes.
ContainerView gltf_container(std::span<const std::byte>);
constexpr int uri_hex(char c) {
    return c >= '0' && c <= '9'   ? c - '0'
           : c >= 'A' && c <= 'F' ? c - 'A' + 10
           : c >= 'a' && c <= 'f' ? c - 'a' + 10
                                  : -1;
}
std::string decode_path_uri(std::string_view);
// Rebase known buffer/image URIs only. Preserve BIN/unknown chunks and opaque JSON.
std::string relocate_gltf_source(const ProjectPaths&, const std::filesystem::path& source,
                                 const std::filesystem::path& destination,
                                 std::span<const std::byte>);
} // namespace forge::gltf_detail
