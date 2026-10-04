#include "bounded_json.hpp"
#include <algorithm>
#include <cmath>
#include <forge/editable_mesh.hpp>
#include <limits>
#include <map>
#include <numbers>
#include <set>
#include <stdexcept>

namespace forge {
namespace {
using Json = nlohmann::json;
using Vec3 = std::array<double, 3>;
constexpr std::size_t vertex_limit = 4096, face_limit = 4096, corner_limit = 16384;
void require(bool value, const char* why) {
    if (!value)
        throw std::runtime_error(why);
}
std::uint32_t id(const Json& value) {
    require(value.is_number_integer() &&
                (value.is_number_unsigned() || value.get<std::int64_t>() >= 0),
            "Mesh element identity must be a positive integer");
    const auto n = value.get<std::uint64_t>();
    require(n && n <= std::numeric_limits<std::uint32_t>::max(),
            "Mesh element identity exceeds supported range");
    return static_cast<std::uint32_t>(n);
}
template <std::size_t N> std::array<double, N> vector(const Json& value) {
    require(value.is_array() && value.size() == N, "Mesh vector has the wrong dimension");
    std::array<double, N> result{};
    for (std::size_t i = 0; i < N; ++i) {
        require(value[i].is_number(), "Mesh vector must contain numbers");
        result[i] = value[i].get<double>();
        require(std::isfinite(result[i]) && std::abs(result[i]) <= 100000.0,
                "Mesh vector is nonfinite or outside editable bounds");
    }
    return result;
}
Vec3 sub(Vec3 a, Vec3 b) { return {a[0] - b[0], a[1] - b[1], a[2] - b[2]}; }
Vec3 cross(Vec3 a, Vec3 b) {
    return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
}
double dot(Vec3 a, Vec3 b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
double length(Vec3 a) { return std::sqrt(dot(a, a)); }
struct Polygon {
    std::vector<Vec3> points;
    Vec3 normal{};
};
Polygon polygon(const Json& face, const std::map<std::uint32_t, Vec3>& positions) {
    Polygon result;
    const auto& corners = face.at("corners");
    for (const auto& corner : corners)
        result.points.push_back(positions.at(id(corner.at("vertex"))));
    const auto basis =
        cross(sub(result.points[1], result.points[0]), sub(result.points[2], result.points[0]));
    const auto size = length(basis);
    require(size > 1e-8, "Mesh face has zero area");
    for (unsigned axis = 0; axis < 3; ++axis)
        result.normal[axis] = basis[axis] / size;
    // The first slice uses planar convex polygons. A failed edit is rejected
    // before it can alter the current source or its cooked revision.
    for (std::size_t i = 0; i < result.points.size(); ++i) {
        const auto& a = result.points[i];
        const auto& b = result.points[(i + 1) % result.points.size()];
        const auto& c = result.points[(i + 2) % result.points.size()];
        require(std::abs(dot(sub(a, result.points[0]), result.normal)) <= 1e-5 &&
                    dot(cross(sub(b, a), sub(c, b)), result.normal) > 1e-8,
                "Mesh face must remain planar and strictly convex");
    }
    return result;
}
Json corner(std::uint32_t vertex, std::array<double, 2> uv) {
    return {{"vertex", vertex}, {"uv", uv}};
}
Json face(std::uint32_t face_id, const std::array<std::uint32_t, 4>& vertices) {
    return {{"id", face_id},
            {"corners", Json::array({corner(vertices[0], {0, 0}), corner(vertices[1], {1, 0}),
                                     corner(vertices[2], {1, 1}), corner(vertices[3], {0, 1})})}};
}
Json* find_face(Json& document, std::uint32_t wanted) {
    for (auto& candidate : document.at("faces"))
        if (id(candidate.at("id")) == wanted)
            return &candidate;
    return nullptr;
}
} // namespace

AssetId EditableMeshSource::asset() const { return document.at("asset_id").get<AssetId>(); }
EditableMeshSource EditableMeshSource::create_cube(AssetId asset_id) {
    EditableMeshSource result{{{"kind", "forge.editable-mesh"},
                               {"version", 1},
                               {"asset_id", asset_id},
                               {"next_id", 15},
                               {"vertices", Json::array()},
                               {"faces", Json::array()}}};
    const std::array<Vec3, 8> positions{{
        {-0.5, -0.5, -0.5},
        {0.5, -0.5, -0.5},
        {0.5, 0.5, -0.5},
        {-0.5, 0.5, -0.5},
        {-0.5, -0.5, 0.5},
        {0.5, -0.5, 0.5},
        {0.5, 0.5, 0.5},
        {-0.5, 0.5, 0.5},
    }};
    for (std::size_t i = 0; i < positions.size(); ++i)
        result.document["vertices"].push_back({{"id", i + 1}, {"position", positions[i]}});
    const std::array<std::array<std::uint32_t, 4>, 6> loops{{
        {5, 6, 7, 8},
        {2, 1, 4, 3},
        {6, 2, 3, 7},
        {1, 5, 8, 4},
        {8, 7, 3, 4},
        {1, 2, 6, 5},
    }};
    for (std::size_t i = 0; i < loops.size(); ++i)
        result.document["faces"].push_back(face(static_cast<std::uint32_t>(i + 9), loops[i]));
    result.validate();
    return result;
}
EditableMeshSource EditableMeshSource::parse(std::span<const std::byte> bytes) {
    EditableMeshSource result{
        asset_detail::parse_bounded_json(bytes, editable_mesh_source_byte_limit, 100000, 16)};
    result.validate();
    return result;
}
void EditableMeshSource::validate() const {
    require(document.is_object() && document.at("kind") == "forge.editable-mesh" &&
                document.at("version") == 1 && bool(asset()),
            "Unsupported editable Mesh source kind/version/identity");
    const auto& vertices = document.at("vertices");
    const auto& faces = document.at("faces");
    require(vertices.is_array() && !vertices.empty() && vertices.size() <= vertex_limit &&
                faces.is_array() && !faces.empty() && faces.size() <= face_limit,
            "Editable Mesh exceeds vertex or face limits");
    std::map<std::uint32_t, Vec3> positions;
    std::set<std::uint32_t> identities;
    std::uint32_t highest = 0;
    for (const auto& v : vertices) {
        const auto key = id(v.at("id"));
        require(identities.insert(key).second &&
                    positions.emplace(key, vector<3>(v.at("position"))).second,
                "Duplicate editable Mesh vertex identity");
        highest = std::max(highest, key);
    }
    std::size_t corners_total = 0;
    std::map<std::pair<std::uint32_t, std::uint32_t>, std::vector<bool>> edges;
    for (const auto& f : faces) {
        const auto key = id(f.at("id"));
        require(identities.insert(key).second, "Duplicate editable Mesh element identity");
        highest = std::max(highest, key);
        const auto& corners = f.at("corners");
        require(corners.is_array() && corners.size() >= 3 && corners.size() <= 16 &&
                    corners_total <= corner_limit - corners.size(),
                "Editable Mesh face/corner limit exceeded");
        corners_total += corners.size();
        std::set<std::uint32_t> unique;
        std::vector<std::uint32_t> loop;
        for (const auto& c : corners) {
            const auto vertex = id(c.at("vertex"));
            require(positions.contains(vertex) && unique.insert(vertex).second,
                    "Mesh face has missing or repeated vertices");
            (void)vector<2>(c.at("uv"));
            loop.push_back(vertex);
        }
        (void)polygon(f, positions);
        for (std::size_t i = 0; i < loop.size(); ++i) {
            const auto a = loop[i], b = loop[(i + 1) % loop.size()];
            const auto edge = std::minmax(a, b);
            auto& uses = edges[edge];
            require(uses.size() < 2, "Mesh has a nonmanifold edge");
            uses.push_back(a < b);
            require(uses.size() < 2 || uses[0] != uses[1],
                    "Adjacent mesh faces have inconsistent winding");
        }
    }
    const auto next = id(document.at("next_id"));
    require(highest < next, "Editable Mesh next identity must exceed existing elements");
    require(document.dump().size() <= editable_mesh_source_byte_limit,
            "Editable Mesh source exceeds its byte limit");
}
MeshData EditableMeshSource::cook() const {
    validate();
    std::map<std::uint32_t, Vec3> positions;
    for (const auto& v : document.at("vertices"))
        positions.emplace(id(v.at("id")), vector<3>(v.at("position")));
    MeshPart part;
    part.topology = MeshTopology::Triangles;
    part.material_slot = 0;
    std::vector<float> position, normal, uv;
    for (const auto& f : document.at("faces")) {
        const auto geometry = polygon(f, positions);
        const auto start = static_cast<std::uint32_t>(position.size() / 3);
        for (const auto& c : f.at("corners")) {
            const auto p = positions.at(id(c.at("vertex")));
            const auto t = vector<2>(c.at("uv"));
            for (unsigned axis = 0; axis < 3; ++axis) {
                position.push_back(static_cast<float>(p[axis]));
                normal.push_back(static_cast<float>(geometry.normal[axis]));
            }
            uv.push_back(static_cast<float>(t[0]));
            uv.push_back(static_cast<float>(t[1]));
        }
        for (std::uint32_t i = 1; i + 1 < f.at("corners").size(); ++i) {
            part.indices.push_back(start);
            part.indices.push_back(start + i);
            part.indices.push_back(start + i + 1);
        }
    }
    part.vertices = static_cast<std::uint32_t>(position.size() / 3);
    part.streams.push_back({"POSITION", 3, std::move(position)});
    part.streams.push_back({"NORMAL", 3, std::move(normal)});
    part.streams.push_back({"TEXCOORD_0", 2, std::move(uv)});
    part.bounds = mesh_bounds(part);
    MeshData result;
    result.material_slots = 1;
    result.lods.push_back({1, {std::move(part)}});
    validate_mesh(result);
    return result;
}
void EditableMeshSource::translate_vertices(std::span<const std::uint32_t> ids,
                                            std::array<double, 3> translation) {
    require(!ids.empty() && ids.size() <= vertex_limit, "Select at least one Mesh vertex");
    for (const auto value : translation)
        require(std::isfinite(value) && std::abs(value) <= 100000.0, "Mesh translation is invalid");
    auto candidate = *this;
    std::set<std::uint32_t> selected(ids.begin(), ids.end());
    require(selected.size() == ids.size(), "Repeated Mesh vertex selection");
    for (auto& v : candidate.document.at("vertices")) {
        const auto key = id(v.at("id"));
        if (!selected.erase(key))
            continue;
        auto p = vector<3>(v.at("position"));
        for (unsigned axis = 0; axis < 3; ++axis)
            p[axis] += translation[axis];
        v["position"] = p;
    }
    require(selected.empty(), "Selected Mesh vertex is missing");
    candidate.validate();
    document = std::move(candidate.document);
}
void EditableMeshSource::transform_vertices(std::span<const std::uint32_t> ids,
                                            std::array<double, 3> translation,
                                            std::array<double, 3> rotation_degrees,
                                            std::array<double, 3> scale) {
    require(!ids.empty() && ids.size() <= vertex_limit, "Select Mesh vertices to transform");
    for (unsigned axis = 0; axis < 3; ++axis)
        require(std::isfinite(translation[axis]) && std::abs(translation[axis]) <= 100000 &&
                    std::isfinite(rotation_degrees[axis]) &&
                    std::abs(rotation_degrees[axis]) <= 36000 && std::isfinite(scale[axis]) &&
                    scale[axis] >= 0.01 && scale[axis] <= 100,
                "Mesh transform is nonfinite or outside editable bounds");
    auto candidate = *this;
    std::set<std::uint32_t> selected(ids.begin(), ids.end());
    require(selected.size() == ids.size(), "Repeated Mesh vertex selection");
    std::array<double, 3> center{};
    for (const auto& vertex : candidate.document.at("vertices"))
        if (selected.contains(id(vertex.at("id")))) {
            const auto p = vector<3>(vertex.at("position"));
            for (unsigned axis = 0; axis < 3; ++axis)
                center[axis] += p[axis];
        }
    std::size_t found = 0;
    for (const auto& vertex : candidate.document.at("vertices"))
        found += selected.contains(id(vertex.at("id"))) ? 1 : 0;
    require(found == selected.size(), "Selected Mesh vertex is missing");
    for (auto& axis : center)
        axis /= found;
    std::array<double, 3> sine{}, cosine{};
    for (unsigned axis = 0; axis < 3; ++axis) {
        const auto radians = rotation_degrees[axis] * std::numbers::pi / 180.0;
        sine[axis] = std::sin(radians);
        cosine[axis] = std::cos(radians);
    }
    for (auto& vertex : candidate.document.at("vertices")) {
        if (!selected.contains(id(vertex.at("id"))))
            continue;
        auto p = vector<3>(vertex.at("position"));
        for (unsigned axis = 0; axis < 3; ++axis)
            p[axis] = (p[axis] - center[axis]) * scale[axis];
        p = {p[0], cosine[0] * p[1] - sine[0] * p[2], sine[0] * p[1] + cosine[0] * p[2]};
        p = {cosine[1] * p[0] + sine[1] * p[2], p[1], -sine[1] * p[0] + cosine[1] * p[2]};
        p = {cosine[2] * p[0] - sine[2] * p[1], sine[2] * p[0] + cosine[2] * p[1], p[2]};
        for (unsigned axis = 0; axis < 3; ++axis)
            p[axis] += center[axis] + translation[axis];
        vertex["position"] = p;
    }
    candidate.validate();
    document = std::move(candidate.document);
}
std::uint32_t EditableMeshSource::extrude_face(std::uint32_t wanted, double distance) {
    require(std::isfinite(distance) && distance > 1e-5 && distance <= 10000,
            "Mesh extrusion distance must be positive and bounded");
    auto candidate = *this;
    auto* source_face = find_face(candidate.document, wanted);
    require(source_face, "Selected Mesh face is missing");
    std::map<std::uint32_t, Vec3> positions;
    for (const auto& v : candidate.document.at("vertices"))
        positions.emplace(id(v.at("id")), vector<3>(v.at("position")));
    const auto normal = polygon(*source_face, positions).normal;
    const auto old = *source_face;
    const auto corners = old.at("corners");
    auto next = id(candidate.document.at("next_id"));
    require(next <= std::numeric_limits<std::uint32_t>::max() - 1 - 2 * corners.size(),
            "Mesh element identities exhausted");
    std::vector<std::uint32_t> old_vertices, new_vertices;
    for (const auto& c : corners) {
        const auto vertex = id(c.at("vertex"));
        auto p = positions.at(vertex);
        for (unsigned axis = 0; axis < 3; ++axis)
            p[axis] += normal[axis] * distance;
        const auto added = next++;
        candidate.document["vertices"].push_back({{"id", added}, {"position", p}});
        old_vertices.push_back(vertex);
        new_vertices.push_back(added);
    }
    const auto cap = next++;
    Json new_corners = Json::array();
    for (std::size_t i = 0; i < corners.size(); ++i)
        new_corners.push_back(corner(new_vertices[i], vector<2>(corners[i].at("uv"))));
    auto& faces = candidate.document.at("faces");
    for (auto it = faces.begin(); it != faces.end(); ++it)
        if (id(it->at("id")) == wanted) {
            faces.erase(it);
            break;
        }
    faces.push_back({{"id", cap}, {"corners", std::move(new_corners)}});
    for (std::size_t i = 0; i < corners.size(); ++i) {
        const auto j = (i + 1) % corners.size();
        const auto width =
            length(sub(positions.at(old_vertices[j]), positions.at(old_vertices[i])));
        faces.push_back({{"id", next++},
                         {"corners", Json::array({corner(old_vertices[i], {0, 0}),
                                                  corner(old_vertices[j], {width, 0}),
                                                  corner(new_vertices[j], {width, distance}),
                                                  corner(new_vertices[i], {0, distance})})}});
    }
    candidate.document["next_id"] = next;
    candidate.validate();
    document = std::move(candidate.document);
    return cap;
}
void EditableMeshSource::transform_uv(std::uint32_t wanted, std::span<const std::size_t> corners,
                                      std::array<double, 2> translation, double angle,
                                      double scale) {
    require(std::isfinite(angle) && std::isfinite(scale) && scale > 1e-5 && scale <= 10000,
            "Mesh UV transform is invalid");
    for (const auto value : translation)
        require(std::isfinite(value), "Mesh UV translation is invalid");
    auto candidate = *this;
    auto* target = find_face(candidate.document, wanted);
    require(target, "Selected Mesh face is missing");
    auto& values = target->at("corners");
    require(!corners.empty() && corners.size() <= values.size(), "Select Mesh UV corners");
    std::set<std::size_t> selected(corners.begin(), corners.end());
    require(selected.size() == corners.size() && *selected.rbegin() < values.size(),
            "Selected Mesh UV corner is invalid");
    std::array<double, 2> center{};
    for (const auto i : selected) {
        const auto uv = vector<2>(values[i].at("uv"));
        center[0] += uv[0];
        center[1] += uv[1];
    }
    center[0] /= selected.size();
    center[1] /= selected.size();
    const auto c = std::cos(angle), s = std::sin(angle);
    for (const auto i : selected) {
        const auto old = vector<2>(values[i].at("uv"));
        const auto x = (old[0] - center[0]) * scale;
        const auto y = (old[1] - center[1]) * scale;
        values[i]["uv"] = {center[0] + c * x - s * y + translation[0],
                           center[1] + s * x + c * y + translation[1]};
    }
    candidate.validate();
    document = std::move(candidate.document);
}
void EditableMeshSource::project_face_uv(std::uint32_t wanted) {
    auto candidate = *this;
    auto* target = find_face(candidate.document, wanted);
    require(target, "Selected Mesh face is missing");
    std::map<std::uint32_t, Vec3> positions;
    for (const auto& v : candidate.document.at("vertices"))
        positions.emplace(id(v.at("id")), vector<3>(v.at("position")));
    const auto normal = polygon(*target, positions).normal;
    // Project along the dominant face-normal axis. This keeps the first
    // authoring workflow deterministic and avoids an implicit UV unwrap solver.
    const auto axis =
        std::max_element(normal.begin(), normal.end(),
                         [](double a, double b) { return std::abs(a) < std::abs(b); }) -
        normal.begin();
    const auto u = (axis + 1) % 3, v = (axis + 2) % 3;
    for (auto& c : target->at("corners")) {
        const auto p = positions.at(id(c.at("vertex")));
        c["uv"] = {p[u], p[v]};
    }
    candidate.validate();
    document = std::move(candidate.document);
}
} // namespace forge
