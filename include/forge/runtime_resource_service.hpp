#pragma once
#include <cstdint>
#include <forge/identity.hpp>
#include <string>
namespace forge {
// CPU admission only. Ready is not GPU/draw readiness. Tokens are process-local,
// world-scoped subscriptions, never serialized asset identities or native pointers.
enum class RuntimeResourceKind : unsigned { Mesh = 1, Material, Texture, Shader, Collision };
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
    virtual bool release(std::uint64_t) = 0;
    virtual void synchronize() = 0;
    virtual void refresh() = 0;
};
} // namespace forge
