#pragma once
#include <forge/asset_importer.hpp>
#include <forge/project_lease.hpp>
#include <forge/subasset_identity.hpp>
#include <thread>

namespace forge {
// Source-controlled import metadata. Selection remains in AssetCatalog; this
// document owns settings, durable member mapping and reproducible input identity.
struct AssetImportSidecar {
    ImportSettingsDocument settings;
    SubassetIdentityDocument identity;
    nlohmann::json build_inputs;
    nlohmann::json unknown = nlohmann::json::object();
    nlohmann::json document() const;
    static AssetImportSidecar parse(std::string_view bytes);
};
struct AssetPublicationTicket {
    AssetId owner;
    std::filesystem::path source;
    std::optional<std::string> catalog_bytes, sidecar_bytes;
};
struct AssetPublicationCandidate {
    AssetPublicationTicket ticket;
    AssetBuildInput input;
    AssetImportSidecar sidecar;
    // Complete root and all its members, including removed-member tombstones.
    std::vector<AssetRecord> records;
    std::vector<ArtifactFile> files;
};
struct AssetPublicationResult {
    AssetCatalog catalog;
    CachedArtifact artifact;
    // Selection succeeded even if deleting its now-completed recovery record failed.
    std::string cleanup_diagnostic;
};
// Writer-thread boundary shared by editor/headless authoring, never the worker.
// The caller retains ProjectLease for this object's entire lifetime. Runtime
// compatibility must be preflighted without mutating a live resource/world.
class AssetPublisher {
  public:
    using Compatibility = std::function<void(const AssetCatalog&, const CachedArtifact&)>;
    explicit AssetPublisher(const ProjectLease& lease);
    AssetPublicationTicket capture(AssetId owner, const std::filesystem::path& source) const;
    AssetPublicationResult publish(AssetPublicationCandidate candidate,
                                   const AssetImporter& importer,
                                   const Compatibility& compatibility, std::stop_token cancel = {});
    // Idempotent. Exact-byte conflicts are reported without overwriting either file.
    // Returns whether a recovery record was processed.
    bool recover();
    static std::filesystem::path sidecar_path(const std::filesystem::path& source);

  private:
    void check_owner() const;
    const ProjectLease& lease_;
    ProjectPaths paths_;
    std::thread::id thread_;
};
} // namespace forge
