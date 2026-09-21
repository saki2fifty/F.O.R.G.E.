#pragma once
#include <flecs.h>
#include <map>
#include <nlohmann/json.hpp>
#include <vector>
namespace forge::detail {
// Explicit, bounded, forward-only named-value migration. Construct and execute
// inside the isolated schema worker; callers publish a validated document later.
// This owns copied Meta projections, not another native registration authority.
class AuthoredMigration {
  public:
    AuthoredMigration(flecs::world&, nlohmann::json source, nlohmann::json target,
                      const nlohmann::json& rules);
    nlohmann::json migrate(const nlohmann::json& value, bool property_intent = false) const;

  private:
    using Json = nlohmann::json;
    using Path = std::vector<std::string>;
    Json source_, target_;
    std::map<Path, std::string> aliases_;
    std::map<Path, Json> defaults_;
    Json convert(const Json& from, const Json& to, const Json& value, const Json* defaults,
                 Path source_path, Path target_path, bool partial) const;
};
} // namespace forge::detail
