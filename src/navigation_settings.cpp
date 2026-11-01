#include <cmath>
#include <forge/navigation_build.hpp>

namespace forge {
void validate_navigation_settings(const NavigationSettings& s) {
    auto range = [](float v, float lo, float hi) { return std::isfinite(v) && v >= lo && v <= hi; };
    if (!range(s.radius, .05f, 5) || !range(s.height, .2f, 10) || !range(s.climb, 0, 2) ||
        !range(s.slope, 0, 60) || !range(s.cell_size, .05f, 1) ||
        !range(s.cell_height, .025f, .5f) || s.climb >= s.height || s.cell_height > s.height / 2 ||
        (s.plane != NavigationPlane::Spatial3D && s.plane != NavigationPlane::PlanarXY))
        throw std::runtime_error("Invalid navigation settings: radius .05-5 m, height .2-10 m, "
                                 "climb 0-2 m below height, slope 0-60 degrees, cells .05-1 m / "
                                 ".025-.5 m; at least two cells of clearance required");
}
void to_json(nlohmann::json& j, const NavigationSettings& s) {
    j = {{"radius", s.radius}, {"height", s.height},       {"climb", s.climb},
         {"slope", s.slope},   {"cell_size", s.cell_size}, {"cell_height", s.cell_height}};
    if (s.plane != NavigationPlane::Spatial3D)
        j["plane"] = unsigned(s.plane);
    if (s.dynamic_obstacles)
        j["dynamic_obstacles"] = true;
}
void from_json(const nlohmann::json& j, NavigationSettings& s) {
    s = {j.at("radius"), j.at("height"),    j.at("climb"),
         j.at("slope"),  j.at("cell_size"), j.at("cell_height")};
    if (j.contains("plane")) {
        const auto& plane = j.at("plane");
        if (!plane.is_number_integer() || (plane != 0 && plane != 1))
            throw std::runtime_error("Navigation plane must be integer 0 (3D) or 1 (XY)");
        s.plane = plane == 1 ? NavigationPlane::PlanarXY : NavigationPlane::Spatial3D;
    }
    if (j.contains("dynamic_obstacles")) {
        if (!j.at("dynamic_obstacles").is_boolean())
            throw std::runtime_error("Dynamic obstacles setting must be boolean");
        s.dynamic_obstacles = j.at("dynamic_obstacles").get<bool>();
    }
    validate_navigation_settings(s);
}
} // namespace forge
