#include "import_authoring.hpp"
#include "audio_authoring.hpp"
#include "audio_importer.hpp"
#include "model_authoring.hpp"
#include "model_importer.hpp"
#include "texture_authoring.hpp"
#include "texture_importer.hpp"
namespace forge {
std::shared_ptr<const AssetImporterRegistry>
asset_import_registry(const std::filesystem::path& worker) {
    auto registry = std::make_shared<AssetImporterRegistry>();
    registry->add(asset_detail::texture_importer(worker, false));
    registry->add(asset_detail::texture_importer(worker, true));
    registry->add(asset_detail::model_importer(worker));
    registry->add(asset_detail::audio_importer(worker));
    registry->seal();
    return registry;
}
void prepare_asset_publication(AssetPublicationCandidate& candidate, const AssetImportPlan& plan,
                               const AssetCatalog& previous_catalog,
                               std::span<const SubassetIdentityDecision> decisions) {
    if (plan.input.importer == "forge.model.gltf")
        prepare_model_publication(candidate, plan, previous_catalog, decisions);
    else if (plan.input.importer == "forge.texture.image" ||
             plan.input.importer == "forge.texture.container") {
        if (!decisions.empty())
            throw std::runtime_error("Texture has no subasset correspondence decisions");
        prepare_texture_publication(candidate, plan);
    } else if (plan.input.importer == "forge.audio.wav") {
        if (!decisions.empty())
            throw std::runtime_error("AudioClip has no subasset correspondence decisions");
        prepare_audio_publication(candidate, plan);
    } else
        throw std::runtime_error("No publication adapter for this importer");
}
} // namespace forge
