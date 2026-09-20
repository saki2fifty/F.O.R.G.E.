#pragma once
#include <forge/derived_cache.hpp>
#include <forge/import_settings.hpp>
#include <memory>
#include <stop_token>

namespace forge {
struct ImportTarget {
    std::string platform, backend, profile;
    auto operator<=>(const ImportTarget&) const = default;
};
struct ImportWorkerLimits {
    std::uint64_t memory_bytes = 1024ull * 1024 * 1024;
    std::uint64_t output_bytes = 512ull * 1024 * 1024;
    unsigned seconds = 120, output_files = 4096;
};
enum class ImportExecution { IsolatedProcess, TrustedCpuTask };
struct AssetImporterDescriptor {
    std::string id, revision, label, description;
    std::vector<std::string> extensions, source_kinds, output_types;
    std::string output_format;
    unsigned output_version = 1;
    unsigned diagnostic_version = 1;
    std::vector<ImportTarget> targets;
    bool deterministic = true;
    ImportExecution execution = ImportExecution::IsolatedProcess;
    ImportWorkerLimits limits;
};
struct ImportProbe {
    std::filesystem::path source;
    std::span<const std::byte> prefix;
};
enum class ImportProbeMatch { No, Possible, Strong };
struct ImportProbeResult {
    ImportProbeMatch match = ImportProbeMatch::No;
    std::string source_kind, reason;
};
struct AssetImportRequest {
    AssetId asset;
    std::filesystem::path project, source;
    ImportTarget target;
    ImportSettingsDocument settings;
};
struct AssetImportPlan {
    AssetBuildInput input;
    std::vector<AssetSourceDependency> sources;
    // Importer-owned versioned candidate data; never catalog authority or ECS state.
    nlohmann::json data = nlohmann::json::object();
};
// Exact-source C++ tooling contract, not a stable third-party plugin ABI. Discovery
// and decode/process/cook run in the declared execution owner. A registry entry
// alone does not execute them or confer permission to mutate the catalog.
class AssetImporter {
  public:
    AssetImporter(AssetImporterDescriptor descriptor, ImportSettingsSchema settings)
        : descriptor_(std::move(descriptor)), settings_(std::move(settings)) {}
    virtual ~AssetImporter() = default;
    const AssetImporterDescriptor& descriptor() const { return descriptor_; }
    const ImportSettingsSchema& settings() const { return settings_; }
    virtual ImportProbeResult probe(const ImportProbe& source) const = 0;
    virtual AssetImportPlan discover(const AssetImportRequest& request,
                                     std::stop_token stop) const = 0;
    virtual std::vector<ArtifactFile>
    import_and_cook(const AssetImportRequest& request, const AssetImportPlan& plan,
                    std::stop_token stop,
                    const std::function<void(double, std::string)>& progress) const = 0;
    virtual void validate(const CachedArtifact& candidate) const = 0;

  private:
    const AssetImporterDescriptor descriptor_;
    const ImportSettingsSchema settings_;
};
struct ImporterCandidate {
    std::shared_ptr<const AssetImporter> importer;
    ImportProbeResult probe;
};
class AssetImporterRegistry {
  public:
    void add(std::shared_ptr<const AssetImporter> importer);
    void seal();
    bool sealed() const { return sealed_; }
    std::vector<AssetImporterDescriptor> descriptors() const;
    std::shared_ptr<const AssetImporter> find(std::string_view id) const;
    // All matches are returned in stable-ID order, never registration order.
    // Byte probes are bounded to64KiB; extension alone never proves valid content.
    std::vector<ImporterCandidate> candidates(const ImportProbe& source,
                                              const ImportTarget& target) const;
    std::shared_ptr<const AssetImporter>
    select(const ImportProbe& source, const ImportTarget& target,
           std::optional<std::string_view> explicit_importer = {}) const;

  private:
    std::map<std::string, std::shared_ptr<const AssetImporter>> entries_;
    bool sealed_ = false;
};
} // namespace forge
