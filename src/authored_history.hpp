#pragma once
#include <filesystem>
#include <nlohmann/json.hpp>
#include <optional>
namespace forge::detail {
// Durable project metadata copied from admitted native schemas. It preserves
// prior versions for explicit migration and reserves removed type keys/owners.
// It neither registers types nor makes a stored schema currently executable.
class AuthoredHistory {
  public:
    explicit AuthoredHistory(std::filesystem::path project);
    nlohmann::json prepare(const nlohmann::json& validated_manifest) const;
    void publish(const nlohmann::json& prepared);
    nlohmann::json declarations() const;
    const nlohmann::json& document() const { return document_; }

  private:
    std::filesystem::path path_;
    std::optional<nlohmann::json> baseline_;
    nlohmann::json document_;
};
} // namespace forge::detail
