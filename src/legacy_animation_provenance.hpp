#pragma once
#include "animation_asset.hpp"
#include <forge/assets.hpp>
namespace forge::animation_detail {
inline void validate_legacy_provenance(const nlohmann::json& m) {
    if (m.at("version") != 1 || m.at("ozz_revision") != ozz_revision ||
        m.at("ozz_version") != "0.17.0" || m.at("converter") != "gltf2ozz" ||
        m.at("converter_revision") != ozz_revision || !m.at("source_asset").get<AssetId>() ||
        !m.at("skeleton_asset").get<AssetId>())
        throw ArchiveError("Unsupported animation provenance");
    for (const auto* field :
         {"source_sha256", "converter_sha256", "artifact_sha256", "skeleton_sha256"})
        if (!valid_content_digest(m.at(field).get<std::string>()))
            throw ArchiveError("Invalid animation provenance digest");
    for (const auto& entry : m.at("settings").at("animations"))
        if (entry.at("iframe_interval") != 0 || entry.at("raw") != false ||
            entry.at("additive") != false)
            throw ArchiveError("Unsupported animation conversion settings");
}
inline void validate_legacy_pair(AssetId skeleton, const nlohmann::json& sm,
                                 const nlohmann::json& cm) {
    validate_legacy_provenance(sm);
    validate_legacy_provenance(cm);
    if (sm.at("skeleton_asset") != nlohmann::json(skeleton) ||
        cm.at("skeleton_asset") != nlohmann::json(skeleton) ||
        sm.at("artifact_sha256") != cm.at("skeleton_sha256") ||
        sm.at("skeleton_sha256") != sm.at("artifact_sha256") ||
        sm.at("source_asset") != cm.at("source_asset") ||
        sm.at("source_sha256") != cm.at("source_sha256") || sm.at("settings") != cm.at("settings"))
        throw ArchiveError("Animation clip is incompatible with this skeleton revision");
}
} // namespace forge::animation_detail
