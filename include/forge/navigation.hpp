#pragma once
#include <forge/world.hpp>
namespace forge {
EngineModule navigation_module(std::filesystem::path project);
class NavigationRuntime final : public NavigationService {
  public:
    NavigationRuntime(WorldContext&, std::filesystem::path);
    ~NavigationRuntime() override;
    void bind(Scene*);  // RuntimeSimulation owns this bounded borrow.
    void synchronize(); // Capture geometry at a writable fixed-tick boundary.
    void tick(float dt, std::uint64_t tick);
    NavResult project_point(AssetRef<NavMeshAsset>, Double3) override;
    NavResult find_path(AssetRef<NavMeshAsset>, Double3, Double3) override;
    Json debug(flecs::entity_t) const;
    Json checkpoint();
    // Restore only into an unpublished candidate world; discard that world on failure.
    void restore(const Json&);
    void shutdown() noexcept;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace forge
