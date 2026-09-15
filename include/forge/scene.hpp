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
    Json schema() const;
    void replace(const Json& document);
    void save(const std::filesystem::path& path) const;
    void load(const std::filesystem::path& path);
    void edit(const Json& document);
    bool undo();
    bool redo();
    void translate(float x, float y, float z);

  private:
    std::unique_ptr<flecs::world> world_;
    Json source_;
    std::map<std::string, flecs::entity_t> entities_;
    std::vector<Json> undo_, redo_;
};
void atomic_write(const std::filesystem::path& path, const std::string& contents);
} // namespace forge
