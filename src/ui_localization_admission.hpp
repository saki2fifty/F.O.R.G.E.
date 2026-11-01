#pragma once
#include "single_source_admission.hpp"
#include <forge/assets.hpp>
#include <forge/derived_cache.hpp>
#include <forge/ui_localization.hpp>

namespace forge {
// The same typed boundary serves candidate UI admission and runtime packaging.
inline UiLocalizationData admit_ui_localization_artifact(const AssetRecord& record,
                                                         const CachedArtifact& artifact) {
    const auto& file = asset_detail::admit_single_source_artifact(
        record, artifact,
        {UiLocalizationAsset::type, "forge.ui.localization", "forge.ui.localization.v1",
         "forge.ui-localization", "localization.json", "forge.ui_localization"});
    const auto source = UiLocalizationSource::parse(file.bytes);
    if (source.asset() != record.id)
        throw std::runtime_error("Cooked UI localization identity mismatch");
    return source.compiled();
}
} // namespace forge
