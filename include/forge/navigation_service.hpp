#pragma once
#include <forge/navigation_components.hpp>
#include <forge/transform.hpp>
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
class NavigationService {
  public:
    virtual ~NavigationService() = default;
    virtual NavResult project_point(AssetRef<NavMeshAsset>, Double3 point) = 0;
    virtual NavResult find_path(AssetRef<NavMeshAsset>, Double3 start, Double3 end) = 0;
};
} // namespace forge
