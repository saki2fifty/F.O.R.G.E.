#include "asset_bytes.hpp"
#include <forge/texture_resource.hpp>
namespace forge {
ResourcePool<TextureAsset>::Loader
texture_resource_loader(std::filesystem::path path, std::string digest, TextureLimits limits) {
    resource_detail::valid_revision(digest);
    if (limits.bytes > SIZE_MAX - 4 * 1024 * 1024 - 24)
        throw std::runtime_error("Texture loader byte limit overflow");
    return [path = std::move(path), digest = std::move(digest), limits](std::stop_token stop) {
        if (stop.stop_requested())
            throw std::runtime_error("Texture resource load cancelled");
        auto bytes = asset_detail::read_bytes(path, limits.bytes + 4 * 1024 * 1024 + 24);
        if (asset_detail::content_digest(bytes) != digest)
            throw std::runtime_error("Cooked texture content digest mismatch");
        if (stop.stop_requested())
            throw std::runtime_error("Texture resource load cancelled");
        auto texture = std::make_unique<TextureData>(decode_texture(bytes, limits));
        if (stop.stop_requested())
            throw std::runtime_error("Texture resource load cancelled");
        const auto used = texture->resident_bytes();
        return ResourceCandidate<TextureAsset>{std::move(texture), ResourceMemory{used}};
    };
}
} // namespace forge
