#include "asset_bytes.hpp"
#include <forge/texture_bundle.hpp>
#include <forge/texture_resource.hpp>
namespace forge {
ResourcePool<TextureAsset>::Loader texture_bundle_resource_loader(std::filesystem::path index_path,
                                                                  std::string index_digest,
                                                                  TextureSemantic semantic,
                                                                  TextureLimits limits) {
    resource_detail::valid_revision(index_digest);
    (void)texture_variant_key(semantic);
    return [index_path = std::move(index_path), index_digest = std::move(index_digest), semantic,
            limits](std::stop_token stop) {
        if (stop.stop_requested())
            throw std::runtime_error("Texture variant load cancelled");
        const auto bytes = asset_detail::read_bytes(index_path, 16384);
        if (asset_detail::content_digest(bytes) != index_digest)
            throw std::runtime_error("Texture bundle index digest mismatch");
        const auto index = decode_texture_bundle_index(bytes);
        const auto& entry = index.find(semantic);
        // Index names are canonical generated filenames; callers still contain the
        // index directory. Resource loading never opens an arbitrary source path.
        const auto file = index_path.parent_path() / entry.file;
        if (std::filesystem::is_symlink(file) || std::filesystem::file_size(file) != entry.bytes)
            throw std::runtime_error("Texture variant file size or ownership changed");
        auto candidate = texture_resource_loader(file, entry.digest, limits)(stop);
        if (candidate.value->semantic != semantic)
            throw std::runtime_error("Texture variant data disagrees with index semantics");
        return candidate;
    };
}
} // namespace forge
