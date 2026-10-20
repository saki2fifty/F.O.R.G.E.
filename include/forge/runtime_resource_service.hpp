#pragma once
#include <cstdint>
#include <forge/identity.hpp>
#include <string>
namespace forge {
struct MeshAsset;
struct MaterialAsset;
struct TileSetAsset;
struct TileMapAsset;
template <class T> class ResourceLease;
// CPU admission only. Ready is not GPU/draw readiness. Tokens are process-local,
// world-scoped subscriptions, never serialized asset identities or native pointers.
enum class RuntimeResourceKind : unsigned {
    Mesh = 1,
    Material,
    Texture,
    Shader,
    Collision,
    TileSet,
    TileMap
};
enum class RuntimeTextureVariant : unsigned { Automatic, Color, Data, Normal, HdrColor };
struct RuntimeResourceStatus {
    std::string state, requested_revision, retained_revision, diagnostic;
    std::uint64_t source_generation = 0;
};
class RuntimeResourceService {
  public:
    virtual ~RuntimeResourceService() = default;
    virtual std::uint64_t request(RuntimeResourceKind, AssetId,
                                  RuntimeTextureVariant = RuntimeTextureVariant::Automatic) = 0;
    virtual RuntimeResourceStatus inspect(std::uint64_t) const = 0;
    // Builtin CPU consumers only; copied retained lease, no IO or adoption.
    // Inspect requested/retained revisions before using last-good data.
    virtual ResourceLease<MeshAsset> mesh(std::uint64_t) const = 0;
    virtual bool release(std::uint64_t) = 0;
    virtual void synchronize() = 0;
    virtual void refresh() = 0;
    // Same retained-lease/read-only boundary as mesh(), for builtin consumers.
    virtual ResourceLease<MaterialAsset> material(std::uint64_t) const = 0;
    virtual ResourceLease<TileSetAsset> tile_set(std::uint64_t) const = 0;
    virtual ResourceLease<TileMapAsset> tile_map(std::uint64_t) const = 0;
};
} // namespace forge
