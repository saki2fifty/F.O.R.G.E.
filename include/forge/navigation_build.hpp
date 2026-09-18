#pragma once
#include <forge/navigation_components.hpp>
#include <forge/transform.hpp>
#include <memory>
#include <stop_token>
namespace forge {
struct NavigationSettings {
    float radius = .4f, height = 2, climb = .4f, slope = 45, cell_size = .2f, cell_height = .1f;
    bool operator==(const NavigationSettings&) const = default;
};
void to_json(nlohmann::json&, const NavigationSettings&);
void from_json(const nlohmann::json&, NavigationSettings&);
void validate_navigation_settings(const NavigationSettings&);
// Evaluated scene snapshot only. Authored components remain the source of truth.
std::string navigation_geometry_digest(const nlohmann::json& effective_scene);
class NavigationCandidate {
  public:
    NavigationCandidate(NavigationCandidate&&) noexcept;
    NavigationCandidate& operator=(NavigationCandidate&&) noexcept;
    ~NavigationCandidate();
    AssetRecord publish(const nlohmann::json& current_effective_scene);

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    explicit NavigationCandidate(std::unique_ptr<Impl>);
    friend NavigationCandidate prepare_navigation(const std::filesystem::path&,
                                                  const nlohmann::json&, NavigationSettings,
                                                  const std::filesystem::path&, std::stop_token);
};
NavigationCandidate prepare_navigation(const std::filesystem::path& project,
                                       const nlohmann::json& effective_scene,
                                       NavigationSettings settings,
                                       const std::filesystem::path& worker,
                                       std::stop_token cancel = {});
// Read-only visualization uses the same admission path as runtime.
std::vector<Double3> navigation_triangles(const std::filesystem::path& project,
                                          AssetRef<NavMeshAsset>);
} // namespace forge
