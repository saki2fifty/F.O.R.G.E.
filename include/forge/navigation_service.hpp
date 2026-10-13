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
class NavigationService {
  public:
    virtual ~NavigationService() = default;
    virtual NavResult project_point(AssetRef<NavMeshAsset>, Double3 point) = 0;
    virtual NavResult find_path(AssetRef<NavMeshAsset>, Double3 start, Double3 end) = 0;
    virtual std::optional<NavigationAgentObservation> agent_state(EntityRef) const = 0;
};
} // namespace forge
