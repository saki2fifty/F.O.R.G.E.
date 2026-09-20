#include "asset_bytes.hpp"
#include <forge/mesh_resource.hpp>
namespace forge {
ResourcePool<MeshAsset>::Loader mesh_resource_loader(std::filesystem::path path, std::string digest,
                                                     MeshLimits limits) {
    resource_detail::valid_revision(digest);
    if (limits.bytes > SIZE_MAX - 8 * 1024 * 1024 - 24)
        throw std::runtime_error("Mesh loader byte limit overflow");
    return [path = std::move(path), digest = std::move(digest), limits](std::stop_token stop) {
        if (stop.stop_requested())
            throw std::runtime_error("Mesh resource load cancelled");
        auto bytes = asset_detail::read_bytes(path, limits.bytes + 8 * 1024 * 1024 + 24);
        if (asset_detail::content_digest(bytes) != digest)
            throw std::runtime_error("Cooked mesh content digest mismatch");
        if (stop.stop_requested())
            throw std::runtime_error("Mesh resource load cancelled");
        auto mesh = std::make_unique<MeshData>(decode_mesh(bytes, limits));
        if (stop.stop_requested())
            throw std::runtime_error("Mesh resource load cancelled");
        const auto used = mesh->resident_bytes();
        return ResourceCandidate<MeshAsset>{std::move(mesh), ResourceMemory{used}};
    };
}
} // namespace forge
