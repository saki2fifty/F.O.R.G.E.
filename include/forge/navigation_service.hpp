#pragma once
#include <forge/navigation_components.hpp>
#include <forge/transform.hpp>
#include <optional>
#include <vector>
namespace forge {
enum class NavStatus : unsigned {
    Success,
    Partial,
    Missing,
    Stale,
    StartOutside,
    EndOutside,
    NoPath,
    Limit,
    Invalid,
    Unavailable
};
const char* nav_status_name(NavStatus status);
struct NavResult {
    NavStatus status = NavStatus::Invalid;
    std::vector<Double3> points;
    std::string diagnostic;
};
enum class NavigationAgentPhase : unsigned {
    Disabled,
    Idle,
    Pending,
    Moving,
    Paused,
    Arrived,
    Blocked,
    Failed
};
const char* navigation_agent_phase_name(NavigationAgentPhase);
struct NavigationAgentObservation {
    NavigationAgentPhase phase = NavigationAgentPhase::Pending;
    NavStatus status = NavStatus::Unavailable;
    std::uint64_t tick = 0;
    bool sampled = false, has_path = false;
    Double3 position{}, velocity{}, projected_destination{};
    double remaining_distance = 0;
};
enum class NavigationRebuildPhase {
    Queued,
    Preparing,
    Building,
    Applied,
    Cancelled,
    Stale,
    Failed
};
const char* navigation_rebuild_phase_name(NavigationRebuildPhase);
struct NavigationRebuildObservation {
    std::uint64_t token = 0;
    AssetId asset;
    NavigationRebuildPhase phase = NavigationRebuildPhase::Queued;
    std::string geometry_revision, diagnostic;
    std::uint64_t query_generation = 0;
};
class NavigationService {
  public:
    virtual ~NavigationService() = default;
    virtual NavResult project_point(AssetRef<NavMeshAsset>, Double3 point) = 0;
    virtual NavResult find_path(AssetRef<NavMeshAsset>, Double3 start, Double3 end) = 0;
    virtual NavResult project_point(AssetRef<NavMeshAsset>, Double3 point,
                                    const NavigationAreaFilter&) = 0;
    virtual NavResult find_path(AssetRef<NavMeshAsset>, Double3 start, Double3 end,
                                const NavigationAreaFilter&) = 0;
    virtual std::optional<NavigationAgentObservation> agent_state(EntityRef) const = 0;
    // Explicit world-local rebuild; state reads never run capture/adoption.
    virtual std::uint64_t request_rebuild(AssetRef<NavMeshAsset>) = 0;
    virtual NavigationRebuildObservation rebuild_state(std::uint64_t) const = 0;
    virtual bool cancel_rebuild(std::uint64_t) = 0;
};
} // namespace forge
