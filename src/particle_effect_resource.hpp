#pragma once
#include <forge/assets.hpp>
#include <forge/derived_cache.hpp>
#include <forge/particle_effect_source.hpp>
#include <forge/resource.hpp>
namespace forge {
template <> struct ResourceTraits<ParticleEffectAsset> {
    using Data = ParticleEffectData;
};
// Shared immutable admission for loading and source-free packaging.
ParticleEffectData admit_particle_effect_artifact(const CachedArtifact&, const AssetRecord&);
ResourceTicket request_particle_effect(ResourcePool<ParticleEffectAsset>&,
                                       const std::filesystem::path&,
                                       std::shared_ptr<const AssetCatalog>,
                                       AssetRef<ParticleEffectAsset>);
} // namespace forge
