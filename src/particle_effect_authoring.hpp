#pragma once
#include "asset_import_service.hpp"
#include "asset_source_document.hpp"
#include <forge/particle_effect_source.hpp>
namespace forge {
struct ParticleEffectDocumentTraits {
    using Source = ParticleEffectSource;
    static constexpr auto byte_limit = particle_effect_byte_limit;
    static constexpr auto asset_type = ParticleEffectAsset::type;
    static constexpr auto suffix = ".particles.json";
    static Source create(AssetId id) { return Source::create(id); }
};
using ParticleEffectDocument = AssetSourceDocument<ParticleEffectDocumentTraits>;
std::shared_ptr<const AssetImporterRegistry> particle_effect_import_registry();
void prepare_particle_effect_publication(AssetPublicationCandidate&, const AssetImportPlan&);
} // namespace forge
