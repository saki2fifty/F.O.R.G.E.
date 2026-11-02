#include "navigation_links.hpp"
#include "navigation_link_shape.hpp"
#include <DetourNavMesh.h>
#include <DetourNavMeshBuilder.h>
#include <algorithm>
#include <cmath>
#include <optional>
#include <stdexcept>

namespace forge::navigation_detail {
namespace {
void require(bool value, const char* why) {
    if (!value)
        throw std::runtime_error(why);
}
std::array<float, 3> read_endpoint(const nlohmann::json& value) {
    require(value.is_array() && value.size() == 3,
            "Navigation link endpoint requires three numbers");
    for (const auto& coordinate : value)
        require(coordinate.is_number(), "Navigation link endpoint requires numeric coordinates");
    return value.get<std::array<float, 3>>();
}
} // namespace
void validate_navigation_links(std::span<const NavigationLinkData> links, MeshSpace space) {
    require(space == MeshSpace::Spatial3D || space == MeshSpace::PlanarXY,
            "Unsupported navigation link plane");
    require(links.size() <= navigation_link_limit, "Navigation exceeds 32 authored links");
    std::optional<EntityId> previous;
    for (const auto& link : links) {
        require(bool(link.entity) && (!previous || *previous < link.entity),
                "Navigation links require unique sorted source identities");
        previous = link.entity;
        validate_link_shape(link.start, link.end, link.radius, space);
    }
}
nlohmann::json navigation_link_values(std::span<const NavigationLinkData> links, MeshSpace space) {
    validate_navigation_links(links, space);
    auto values = nlohmann::json::array();
    for (const auto& link : links)
        values.push_back({{"entity", link.entity},
                          {"start", link.start},
                          {"end", link.end},
                          {"radius", link.radius},
                          {"bidirectional", link.bidirectional}});
    return values;
}
std::vector<NavigationLinkData> read_navigation_links(const nlohmann::json& values,
                                                      MeshSpace space) {
    require(values.is_array() && values.size() <= navigation_link_limit,
            "Navigation links require an array of at most 32 entries");
    std::vector<NavigationLinkData> links;
    links.reserve(values.size());
    for (const auto& value : values) {
        require(value.is_object() && value.size() == 5 && value.contains("entity") &&
                    value.contains("start") && value.contains("end") && value.contains("radius") &&
                    value.contains("bidirectional") && value.at("radius").is_number() &&
                    value.at("bidirectional").is_boolean(),
                "Invalid navigation link metadata fields");
        links.push_back({value.at("entity").get<EntityId>(), read_endpoint(value.at("start")),
                         read_endpoint(value.at("end")), value.at("radius").get<float>(),
                         value.at("bidirectional").get<bool>()});
    }
    validate_navigation_links(links, space);
    return links;
}
NativeNavigationLinks::NativeNavigationLinks(std::span<const NavigationLinkData> links,
                                             MeshSpace space, float flat_altitude)
    : count_(links.size()) {
    validate_navigation_links(links, space);
    require(std::isfinite(flat_altitude) && std::abs(flat_altitude) <= 4096,
            "Navigation link native altitude exceeds bounds");
    for (std::size_t i = 0; i < count_; ++i) {
        for (unsigned end = 0; end < 2; ++end) {
            const auto& world = end ? links[i].end : links[i].start;
            auto point = native_coordinates(space, {world[0], world[1], world[2]});
            if (space == MeshSpace::PlanarXY)
                point[1] = flat_altitude;
            for (unsigned axis = 0; axis < 3; ++axis)
                vertices_[i * 6 + end * 3 + axis] = float(point[axis]);
        }
        radii_[i] = links[i].radius;
        flags_[i] = 1;
        directions_[i] = links[i].bidirectional ? DT_OFFMESH_CON_BIDIR : 0;
        ids_[i] = unsigned(i + 1);
    }
}
void NativeNavigationLinks::apply(dtNavMeshCreateParams& params) const {
    params.offMeshConCount = int(count_);
    params.offMeshConVerts = count_ ? vertices_.data() : nullptr;
    params.offMeshConRad = count_ ? radii_.data() : nullptr;
    params.offMeshConFlags = count_ ? flags_.data() : nullptr;
    params.offMeshConAreas = count_ ? areas_.data() : nullptr;
    params.offMeshConDir = count_ ? directions_.data() : nullptr;
    params.offMeshConUserID = count_ ? ids_.data() : nullptr;
}
} // namespace forge::navigation_detail
