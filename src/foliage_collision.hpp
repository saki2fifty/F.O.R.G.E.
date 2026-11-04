#pragma once
#include <forge/authoring.hpp>
#include <forge/physics_service.hpp>
#include <forge/project_lease.hpp>
#include <memory>
namespace forge {
struct FoliageCollisionSettings {
    double radius = .25;
    Double3 offset{0, .5, 0};
    std::uint32_t mask = UINT32_MAX;
    bool include_sensors = false;
    EntityId surface;
    std::vector<EntityId> ignored;
};
// One transient owner-thread consumer of existing runtime physics/resources.
// It prepares copied scatter arguments; Scene publication remains mesh.scatter.
class FoliageCollisionPreparation {
  public:
    FoliageCollisionPreparation();
    ~FoliageCollisionPreparation();
    Json start(std::shared_ptr<const ProjectLease>, const Scene&, Json arguments,
               FoliageCollisionSettings = {});
    void poll(const Scene&, const std::filesystem::path& project);
    Json read(const Scene&, const std::filesystem::path& project);
    void cancel(std::string reason = "Cancelled by user");
    bool busy() const;
    Json state() const;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace forge
