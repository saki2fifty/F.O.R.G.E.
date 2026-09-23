#pragma once
#include <forge/assets.hpp>
#include <forge/project_paths.hpp>
#include <forge/runtime_content_access.hpp>
#include <forge/ui_components.hpp>
#include <map>
#include <set>
#include <span>
namespace forge {
void validate_ui_text(std::string_view text, bool markup);
struct UiImage {
    unsigned width = 0, height = 0;
    std::vector<std::byte> rgba;
};
UiImage decode_ui_image(std::span<const std::byte>);
void validate_ui_font(std::span<const std::byte>);
// Observed admitted files, not a second dependency graph or a persisted format.
// A presenter snapshot covers its complete set of active documents conservatively.
struct UiSourceInfo {
    std::filesystem::path source;
    std::string type, digest;
    std::size_t bytes = 0;
    nlohmann::json details = nlohmann::json::object();
    bool operator==(const UiSourceInfo&) const = default;
};
struct UiAssetSnapshot {
    std::filesystem::path project;
    std::map<AssetId, std::filesystem::path> documents;
    std::vector<UiSourceInfo> sources;
    // Reliable native static subset, never an exhaustive declaration. Old
    // snapshots/observations have an empty set and are never auto-promoted.
    std::set<std::filesystem::path> automatic_sources;
};
// Admitted immutable byte copies. Each candidate owns its files and bounded budget.
class UiResources {
  public:
    explicit UiResources(std::filesystem::path project) : paths_(project), access_(project) {}
    std::string document(AssetRef<UiDocumentAsset>);
    const std::vector<std::byte>& read(const std::string& locator);
    std::string join(const std::string& base, const std::string& resource) const;
    std::size_t bytes() const { return bytes_; }
    std::size_t source_count() const { return files_.size(); }
    UiAssetSnapshot snapshot() const;
    void automatic(const std::string& locator);

  private:
    ProjectPaths paths_;
    RuntimeContentAccess access_;
    std::set<std::filesystem::path> automatic_;
    std::map<std::string, std::vector<std::byte>> files_;
    std::map<AssetId, std::filesystem::path> documents_;
    std::size_t bytes_ = 0;
};
AssetRecord create_ui_example(const std::filesystem::path& project);
AssetRecord register_ui_document(const std::filesystem::path& project,
                                 const std::filesystem::path& source);
} // namespace forge
