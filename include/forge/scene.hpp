#pragma once
#include <filesystem>
#include <flecs.h>
#include <map>
#include <memory>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>
namespace forge {
using Json = nlohmann::json;
struct Position {
    float x{}, y{}, z{};
};
struct StableId {
    std::string value;
};
class Scene {
  public:
    Scene();
    flecs::world& world() { return *world_; }
    Json document() const;
    std::uint64_t revision() const { return revision_; }
    void reset(const Json& document);
    std::size_t entity_count() const { return entities_.size(); }
    Json schema() const;
    void replace(const Json& document);
    void save(const std::filesystem::path& path) const;
    void load(const std::filesystem::path& path);
    void edit(const Json& document);
    void rename_entity(const std::string& id, const std::string& name);
    void reparent_entity(const std::string& id, const std::string& parent);
    std::string duplicate_subtree(const std::string& id);
    void delete_subtree(const std::string& id);
    bool undo();
    bool redo();
    void translate(float x, float y, float z);

  private:
    std::uint64_t revision_ = 0;
    std::unique_ptr<flecs::world> world_;
    Json source_;
    std::map<std::string, flecs::entity_t> entities_;
    std::vector<Json> undo_, redo_;
};
void atomic_write(const std::filesystem::path& path, const std::string& contents);
} // namespace forge
