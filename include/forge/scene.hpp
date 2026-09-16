#pragma once
#include <cstdint>
#include <filesystem>
#include <forge/world.hpp>
#include <map>
#include <memory>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>
namespace forge {
using Json = nlohmann::json;
class Scene {
  public:
    explicit Scene(WorldContext& context);
    ~Scene();
    Scene(const Scene&) = delete;
    Scene& operator=(const Scene&) = delete;
    flecs::world& world() const { return context_.world(); }
    flecs::entity entity(const std::string& id) const;
    Json effective_document() const;
    // Detached command/gesture projection; never a mutable live representation.
    Json preview_document(const Json& intended) const;
    static void validate_document(const Json& document);
    Json document() const;
    std::uint64_t revision() const;
    void reset(const Json& document);
    std::size_t entity_count() const;
    Json schema() const;
    void replace(const Json& document);
    void save(const std::filesystem::path& path) const;
    void load(const std::filesystem::path& path);
    void edit(const Json& document);
    void rename_entity(const std::string& id, const std::string& name);
    void reparent_entity(const std::string& id, const std::string& parent);
    std::string duplicate_subtree(const std::string& id);
    void delete_subtree(const std::string& id);
    bool can_undo() const { return !undo_.empty(); }
    bool can_redo() const { return !redo_.empty(); }
    bool undo();
    bool redo();
    void translate(float x, float y, float z);

  private:
    WorldContext& context_;
    flecs::entity_t membership_;
    std::map<std::string, flecs::entity_t>& entities_;
    mutable std::uint64_t revision_ = 0, observed_serial_ = 0;
    Json opaque_;
    Json serialize(bool effective) const;
    void committed();
    std::vector<Json> undo_, redo_;
};
void atomic_write(const std::filesystem::path& path, const std::string& contents);
} // namespace forge
