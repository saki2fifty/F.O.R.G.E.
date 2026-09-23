#pragma once
#include <forge/assets.hpp>
namespace forge {
// Immutable package file admission. Development mode retains ProjectPaths policy;
// presence of a package manifest makes membership/hash checks mandatory.
class RuntimeContentAccess {
  public:
    explicit RuntimeContentAccess(std::filesystem::path root);
    bool packaged() const { return files_.has_value(); }
    std::vector<std::byte> read(const std::filesystem::path& locator, std::size_t limit) const;

  private:
    ProjectPaths paths_;
    std::optional<nlohmann::json> files_;
};
} // namespace forge
