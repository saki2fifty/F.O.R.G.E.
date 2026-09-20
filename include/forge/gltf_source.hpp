#pragma once
#include <forge/asset_build.hpp>
#include <memory>
#include <stop_token>

namespace forge {
struct GltfSourceLimits {
    std::size_t file_bytes = 512 * 1024 * 1024;
    std::size_t total_bytes = 1024 * 1024 * 1024;
    std::size_t json_bytes = 64 * 1024 * 1024;
    std::size_t source_files = 4096;
    std::size_t array_entries = 100000;
};
struct GltfByteRange {
    std::shared_ptr<const std::vector<std::byte>> storage;
    std::size_t offset = 0, length = 0;
    std::span<const std::byte> bytes() const;
};
struct GltfEncodedImage {
    GltfByteRange encoded;
    std::string mime_type;
};
// Immutable captured source inputs. No pixels, GPU resources or ECS objects.
// Container admission is not full glTF semantic/mesh/extension validation.
struct GltfSourceBundle {
    nlohmann::json document;
    std::filesystem::path source;
    std::string source_digest;
    bool binary_container = false;
    std::vector<GltfByteRange> buffers;
    std::vector<GltfEncodedImage> images;
    std::vector<AssetSourceDependency> dependencies;
    std::vector<std::string> optional_extensions;
    std::vector<std::string> diagnostics;
    std::size_t captured_bytes = 0;
};
// Captures project-contained external resources and data URIs before a native
// parser sees them. Network/file schemes are rejected, never fetched. Required
// extension support must be explicitly supplied by the actual importer profile.
GltfSourceBundle
capture_gltf_source(const std::filesystem::path& project, const std::filesystem::path& source,
                    const std::set<std::string>& supported_required_extensions = {},
                    GltfSourceLimits limits = {}, std::stop_token cancel = {});
} // namespace forge
