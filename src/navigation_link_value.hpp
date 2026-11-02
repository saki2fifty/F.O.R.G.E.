#pragma once
#include "navigation_link_shape.hpp"
#include "navigation_links.hpp"
#include "navigation_profile.hpp"
#include <nlohmann/json.hpp>
namespace forge::navigation_detail {
inline void validate_link_value(const NavigationLink& value) {
    validate_profile_name(value.profile);
    validate_link_shape({value.start_x, value.start_y, value.start_z},
                        {value.end_x, value.end_y, value.end_z}, value.radius,
                        MeshSpace(value.plane));
}
inline nlohmann::json link_value(const NavigationLink& value) {
    validate_link_value(value);
    return {{"enabled", value.enabled}, {"bidirectional", value.bidirectional},
            {"plane", value.plane},     {"profile", value.profile},
            {"start_x", value.start_x}, {"start_y", value.start_y},
            {"start_z", value.start_z}, {"end_x", value.end_x},
            {"end_y", value.end_y},     {"end_z", value.end_z},
            {"radius", value.radius}};
}
inline NavigationLink link_from_value(const nlohmann::json& value) {
    NavigationLink link;
    const auto& plane = value.at("plane");
    if (!plane.is_number_integer() || plane < 0 || plane > 1)
        throw std::runtime_error("Navigation link Plane must be 3D or 2D (XY)");
    for (const char* field : {"start_x", "start_y", "start_z", "end_x", "end_y", "end_z", "radius"})
        if (!value.at(field).is_number())
            throw std::runtime_error("Navigation link coordinates and radius must be numeric");
    link.enabled = value.at("enabled").get<bool>();
    link.bidirectional = value.at("bidirectional").get<bool>();
    link.plane = plane.get<std::uint32_t>();
    link.profile = value.at("profile").get<std::string>();
    link.start_x = value.at("start_x").get<float>();
    link.start_y = value.at("start_y").get<float>();
    link.start_z = value.at("start_z").get<float>();
    link.end_x = value.at("end_x").get<float>();
    link.end_y = value.at("end_y").get<float>();
    link.end_z = value.at("end_z").get<float>();
    link.radius = value.at("radius").get<float>();
    validate_link_value(link);
    return link;
}
inline NavigationLinkData derive_link(EntityId entity, const NavigationLink& value,
                                      const AffineTransform& transform) {
    validate_link_value(value);
    const auto start = transform.point({value.start_x, value.start_y, value.start_z});
    const auto end = transform.point({value.end_x, value.end_y, value.end_z});
    NavigationLinkData link{entity, {}, {}, value.radius, value.bidirectional};
    for (unsigned axis = 0; axis < 3; ++axis) {
        link.start[axis] =
            value.plane == unsigned(MeshSpace::PlanarXY) && axis == 2 ? 0 : float(start[axis]);
        link.end[axis] =
            value.plane == unsigned(MeshSpace::PlanarXY) && axis == 2 ? 0 : float(end[axis]);
    }
    validate_link_shape(link.start, link.end, link.radius, MeshSpace(value.plane));
    return link;
}
} // namespace forge::navigation_detail
