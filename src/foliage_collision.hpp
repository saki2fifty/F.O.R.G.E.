#pragma once
#include <array>
#include <forge/authoring.hpp>
#include <forge/physics_service.hpp>
#include <forge/project_lease.hpp>
#include <memory>
#include <span>
#include <string_view>
#include <vector>
namespace forge {
struct MeshBounds;
struct MeshResourceData;
// Static published-geometry admission shared by bounds preparation and its consumers.
using FoliageTriangle = std::array<Double3, 3>;
inline constexpr std::size_t foliage_surface_triangle_limit = 4096;
inline constexpr unsigned foliage_shape_query_limit = 8192;
MeshBounds foliage_template_bounds(const MeshResourceData&,
                                   std::vector<FoliageTriangle>* surfaces = nullptr);
enum class FoliageClearanceProfile {
    WorldSphere,
    MeshBounds,
    CircleXY,
    MeshBoundsXY,
    MeshSurfaces,
    MeshSurfacesXY
};
enum class FoliageClearanceGeometry { Radius, Bounds, Surfaces };
struct FoliageClearanceChoice {
    FoliageClearanceProfile profile;
    const char* key;
    const char* label;
    FoliageClearanceGeometry geometry;
    bool xy;
    bool uses_mesh() const { return geometry != FoliageClearanceGeometry::Radius; }
    bool uses_surfaces() const { return geometry == FoliageClearanceGeometry::Surfaces; }
};
std::span<const FoliageClearanceChoice> foliage_clearance_choices();
const FoliageClearanceChoice& foliage_clearance_choice(FoliageClearanceProfile);
FoliageClearanceProfile foliage_clearance_profile(std::string_view);
struct FoliageCollisionSettings {
    double radius = .25;
    Double3 offset{0, .5, 0};
    std::uint32_t mask = UINT32_MAX;
    bool include_sensors = false;
    EntityId surface;
    std::vector<EntityId> ignored;
    bool avoid_new_overlaps = false;
    FoliageClearanceProfile profile = FoliageClearanceProfile::WorldSphere;
    double padding = 0;
};
// Canonical copied choices for request/gesture fingerprints and result inspection.
Json foliage_collision_settings(const FoliageCollisionSettings&);
// One transient owner-thread consumer of existing runtime physics/resources.
// It prepares copied scatter/stroke arguments; ordinary Scene commands publish them.
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
