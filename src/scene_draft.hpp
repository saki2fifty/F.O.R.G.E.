#pragma once
#include <forge/scene.hpp>
namespace forge::detail {
// Private, short-lived command preparation. No world, no registration, no history,
// no subscriptions. Discarded on failure; only Scene::edit commits a validated batch.
class SceneDraft {
  public:
    explicit SceneDraft(const Scene& source);
    Json document() const { return document_; }
    Json schema() const { return schema_; }
    Json effective_document() const;
    std::size_t entity_count() const { return document_.at("entities").size(); }
    void edit(const Json& document);
    void rename_entity(const std::string& id, const std::string& name);
    void reparent_entity(const std::string& id, const std::string& parent);
    std::string duplicate_subtree(const std::string& id);
    void delete_subtree(const std::string& id);

  private:
    Json document_, schema_;
};
} // namespace forge::detail
