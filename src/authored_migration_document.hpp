#pragma once
#include <forge/scene.hpp>
namespace forge::detail {
// One detached authored document, never a coordinated scene+prefab transaction.
// The same adapter prepares scene history edits and prefab source drafts; the
// latter still require the existing single-asset revision/publication boundary.
class AuthoredMigrationDocument {
  public:
    AuthoredMigrationDocument(Json document, Json source, Json target);
    const Json& values() const { return values_; }
    const Json& before() const { return before_; }
    Json candidate(const Json& returned_values) const;
    void apply(Scene&, const Json& returned_values, std::uint64_t expected_revision) const;

  private:
    Json before_, source_, target_, values_ = Json::array();
    std::string rows_, key_;
    struct Location {
        std::size_t row;
        bool partial;
    };
    std::vector<Location> locations_;
};
} // namespace forge::detail
