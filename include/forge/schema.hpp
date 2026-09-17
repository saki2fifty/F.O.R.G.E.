#pragma once
#include <functional>
#include <map>
#include <nlohmann/json.hpp>
namespace forge {
struct DocumentSchema {
    std::string id;
    unsigned current_version;
    std::vector<unsigned> readable_versions;
    std::string migration_policy;
    std::function<void(const nlohmann::json&)> validate;
    std::function<nlohmann::json(const nlohmann::json&)> migrate;
};
// Detached data only. Filesystem journals/backups and publication remain format-owned.
class SchemaRegistry {
  public:
    void add(DocumentSchema schema);
    nlohmann::json describe() const;
    nlohmann::json prepare(const std::string& kind, const nlohmann::json& source) const;

  private:
    std::map<std::string, DocumentSchema> schemas_;
};
SchemaRegistry core_document_schemas();
} // namespace forge
