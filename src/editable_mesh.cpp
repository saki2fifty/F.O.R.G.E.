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
std::array<double, 4> vertex_color(const Json& value) {
    const auto result = vector<4>(value);
    for (const auto channel : result)
        require(channel >= 0 && channel <= 1, "Mesh vertex color must be within [0,1]");
    return result;
}
void validate_brush(std::span<const MeshBrushSample> samples, double radius) {
    require(!samples.empty() && samples.size() <= 256 && std::isfinite(radius) && radius > 1e-4 &&
                radius <= 1000,
            "Mesh brush stroke is empty or outside bounded settings");
    for (const auto& sample : samples) {
        (void)vector<3>(Json(sample.point));
        require(std::isfinite(sample.pressure) && sample.pressure >= 0 && sample.pressure <= 1,
                "Mesh brush pressure must be within [0,1]");
    }
}
double brush_influence(Vec3 point, std::span<const MeshBrushSample> samples, double radius,
                       MeshBrushSymmetry symmetry) {
    double influence = 0;
    for (const auto& sample : samples) {
        const auto axes = static_cast<std::uint8_t>(symmetry);
        auto difference = [&](unsigned axis) {
            // Nearest of the independently reflected sample coordinates. The
            // existing max influence is exactly the union of all mirror stamps.
            return axes & (1u << axis) ? std::abs(point[axis]) - std::abs(sample.point[axis])
                                       : point[axis] - sample.point[axis];
        };
        const double dx = difference(0), dy = difference(1), dz = difference(2);
        const auto distance = std::sqrt(dx * dx + dy * dy + dz * dz);
        if (distance < radius) {
            const auto falloff = 1 - distance / radius;
            influence = std::max(influence, sample.pressure * falloff * falloff);
        }
    }
    return influence;
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
Json triangle(std::uint32_t face_id, std::array<std::uint32_t, 3> vertices,
              std::array<std::array<double, 2>, 3> uv) {
    return {{"id", face_id},
            {"corners", Json::array({corner(vertices[0], uv[0]), corner(vertices[1], uv[1]),
                                     corner(vertices[2], uv[2])})}};
}
std::map<std::uint32_t, Vec3> averaged_normals(const Json& source,
                                               const std::map<std::uint32_t, Vec3>& positions) {
    std::map<std::uint32_t, Vec3> normals;
    for (const auto& face : source.at("faces")) {
        const auto geometry = polygon(face, positions);
        for (const auto& corner : face.at("corners")) {
            auto& normal = normals[id(corner.at("vertex"))];
            for (unsigned axis = 0; axis < 3; ++axis)
                normal[axis] += geometry.normal[axis];
        }
    }
    for (auto& [key, normal] : normals) {
        (void)key;
        const auto size = length(normal);
        require(size > 1e-8, "Mesh vertex normal is undefined");
        for (auto& axis : normal)
            axis /= size;
    }
    return normals;
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
EditableMeshSource EditableMeshSource::create_sculpt_sphere(AssetId asset_id) {
    constexpr std::uint32_t segments = 16, latitudes = 8;
    EditableMeshSource result{{{"kind", "forge.editable-mesh"},
                               {"version", 1},
                               {"asset_id", asset_id},
                               {"smooth", true},
                               {"vertices", Json::array()},
                               {"faces", Json::array()}}};
    std::uint32_t next = 1;
    const auto add_vertex = [&](Vec3 position) {
        const auto key = next++;
        result.document["vertices"].push_back({{"id", key}, {"position", position}});
        return key;
    };
    const auto top = add_vertex({0, .5, 0});
    std::array<std::array<std::uint32_t, segments>, latitudes - 1> rings{};
    for (std::uint32_t row = 1; row < latitudes; ++row) {
        const double latitude = std::numbers::pi * row / latitudes;
        for (std::uint32_t column = 0; column < segments; ++column) {
            const double longitude = 2 * std::numbers::pi * column / segments;
            rings[row - 1][column] =
                add_vertex({.5 * std::sin(latitude) * std::sin(longitude), .5 * std::cos(latitude),
                            .5 * std::sin(latitude) * std::cos(longitude)});
        }
    }
    const auto bottom = add_vertex({0, -.5, 0});
    const auto add_triangle = [&](std::array<std::uint32_t, 3> vertices,
                                  std::array<std::array<double, 2>, 3> uv) {
        result.document["faces"].push_back(triangle(next++, vertices, uv));
    };
    for (std::uint32_t column = 0; column < segments; ++column) {
        const auto next_column = (column + 1) % segments;
        const double u0 = double(column) / segments, u1 = double(column + 1) / segments;
        add_triangle({top, rings[0][column], rings[0][next_column]},
                     {{{(u0 + u1) * .5, 0}, {u0, 1.0 / latitudes}, {u1, 1.0 / latitudes}}});
        for (std::uint32_t row = 0; row + 1 < latitudes - 1; ++row) {
            const double v0 = double(row + 1) / latitudes;
            const double v1 = double(row + 2) / latitudes;
            add_triangle({rings[row][column], rings[row + 1][column], rings[row + 1][next_column]},
                         {{{u0, v0}, {u0, v1}, {u1, v1}}});
            add_triangle({rings[row][column], rings[row + 1][next_column], rings[row][next_column]},
                         {{{u0, v0}, {u1, v1}, {u1, v0}}});
        }
        add_triangle({rings[latitudes - 2][column], bottom, rings[latitudes - 2][next_column]},
                     {{{u0, double(latitudes - 1) / latitudes},
                       {(u0 + u1) * .5, 1},
                       {u1, double(latitudes - 1) / latitudes}}});
    }
    result.document["next_id"] = next;
    result.validate();
    return result;
}
EditableMeshSource EditableMeshSource::parse(std::span<const std::byte> bytes) {
    EditableMeshSource result{
        asset_detail::parse_bounded_json(bytes, editable_mesh_source_byte_limit, 500000, 16)};
    result.validate();
    return result;
}
void EditableMeshSource::validate() const {
    require(document.is_object() && document.at("kind") == "forge.editable-mesh" &&
                document.at("version") == 1 && bool(asset()),
            "Unsupported editable Mesh source kind/version/identity");
    if (document.contains("smooth"))
        require(document.at("smooth").is_boolean(), "Mesh smooth shading flag must be boolean");
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
        if (v.contains("color"))
            (void)vertex_color(v.at("color"));
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
    const auto smooth_normals = document.value("smooth", false)
                                    ? averaged_normals(document, positions)
                                    : std::map<std::uint32_t, Vec3>{};
    std::map<std::uint32_t, std::array<double, 4>> colors;
    for (const auto& vertex : document.at("vertices"))
        if (vertex.contains("color"))
            colors.emplace(id(vertex.at("id")), vertex_color(vertex.at("color")));
    MeshPart part;
    part.topology = MeshTopology::Triangles;
    part.material_slot = 0;
    std::vector<float> position, normal, uv, color;
    for (const auto& f : document.at("faces")) {
        const auto geometry = polygon(f, positions);
        const auto start = static_cast<std::uint32_t>(position.size() / 3);
        for (const auto& c : f.at("corners")) {
            const auto p = positions.at(id(c.at("vertex")));
            const auto t = vector<2>(c.at("uv"));
            const auto& shading_normal =
                smooth_normals.empty() ? geometry.normal : smooth_normals.at(id(c.at("vertex")));
            for (unsigned axis = 0; axis < 3; ++axis) {
                position.push_back(static_cast<float>(p[axis]));
                normal.push_back(static_cast<float>(shading_normal[axis]));
            }
            uv.push_back(static_cast<float>(t[0]));
            uv.push_back(static_cast<float>(t[1]));
            if (!colors.empty()) {
                const auto paint = colors.contains(id(c.at("vertex")))
                                       ? colors.at(id(c.at("vertex")))
                                       : std::array<double, 4>{1, 1, 1, 1};
                for (const auto channel : paint)
                    color.push_back(static_cast<float>(channel));
            }
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
    if (!colors.empty())
        part.streams.push_back({"COLOR_0", 4, std::move(color)});
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
std::uint32_t EditableMeshSource::inset_face(std::uint32_t face_id, double distance) {
    auto& source = *this;
    source.validate();
    require(std::isfinite(distance) && distance > 0 && distance <= 100000,
            "Inset distance must be finite and positive");
    const auto& doc = source.document;
    const auto found = std::find_if(doc.at("faces").begin(), doc.at("faces").end(),
                                    [&](const Json& f) { return id(f.at("id")) == face_id; });
    require(found != doc.at("faces").end(), "Inset face does not exist");
    const auto selected = *found;
    const auto& corners = selected.at("corners");
    const auto count = corners.size();
    std::size_t total_corners = 0;
    for (const auto& f : doc.at("faces"))
        total_corners += f.at("corners").size();
    const auto next_id = id(doc.at("next_id"));
    require(doc.at("vertices").size() + count <= vertex_limit &&
                doc.at("faces").size() + count <= face_limit &&
                total_corners + 4 * count <= corner_limit &&
                std::uint64_t(next_id) + 2 * count <= std::numeric_limits<std::uint32_t>::max(),
            "Inset exceeds editable Mesh element limits");
    std::map<std::uint32_t, Vec3> positions;
    std::map<std::uint32_t, std::array<double, 4>> colors;
    bool has_color = false;
    for (const auto& v : doc.at("vertices")) {
        const auto identity = id(v.at("id"));
        positions.emplace(identity, vector<3>(v.at("position")));
        colors.emplace(identity, v.contains("color") ? vertex_color(v.at("color"))
                                                     : std::array<double, 4>{1, 1, 1, 1});
        has_color |= v.contains("color");
    }
    const auto shape = polygon(selected, positions);
    std::vector<Vec3> inward(count), inner(count);
    for (std::size_t i = 0; i < count; ++i) {
        auto edge = sub(shape.points[(i + 1) % count], shape.points[i]);
        const auto size = length(edge);
        require(size > 1e-8, "Inset edge is too short");
        for (auto& axis : edge)
            axis /= size;
        inward[i] = cross(shape.normal, edge);
    }
    for (std::size_t i = 0; i < count; ++i) {
        const auto previous = (i + count - 1) % count;
        const auto denominator = 1 + dot(inward[previous], inward[i]);
        require(denominator > 1e-10, "Inset corner is numerically unstable");
        for (unsigned axis = 0; axis < 3; ++axis)
            inner[i][axis] = shape.points[i][axis] +
                             distance * (inward[previous][axis] + inward[i][axis]) / denominator;
        (void)vector<3>(Json(inner[i]));
        for (std::size_t j = 0; j < count; ++j)
            require(dot(sub(inner[i], shape.points[j]), inward[j]) >=
                        distance - std::max(1e-10, distance * 1e-9),
                    "Inset collapses or crosses the face boundary");
    }
    auto candidate = source;
    auto& edited = candidate.document;
    std::vector<Json> inner_corners;
    auto allocator = next_id;
    for (std::size_t i = 0; i < count; ++i) {
        std::array<double, 3> weights{};
        std::size_t triangle_index = 0;
        bool admitted = false;
        for (std::size_t k = 1; k + 1 < count; ++k) {
            const auto b = sub(shape.points[k], shape.points[0]);
            const auto c = sub(shape.points[k + 1], shape.points[0]);
            const auto p = sub(inner[i], shape.points[0]);
            const auto area = dot(cross(b, c), shape.normal);
            const auto wb = dot(cross(p, c), shape.normal) / area;
            const auto wc = dot(cross(b, p), shape.normal) / area;
            weights = {1 - wb - wc, wb, wc};
            if (std::all_of(weights.begin(), weights.end(), [](double w) { return w >= -1e-9; })) {
                for (auto& w : weights)
                    w = std::max(0., w);
                const auto sum = weights[0] + weights[1] + weights[2];
                for (auto& w : weights)
                    w /= sum;
                triangle_index = k;
                admitted = true;
                break;
            }
        }
        require(admitted, "Inset corner lies outside the source face");
        const std::array<std::size_t, 3> indices{0, triangle_index, triangle_index + 1};
        std::array<double, 2> uv{};
        std::array<double, 4> color{};
        for (unsigned j = 0; j < 3; ++j) {
            const auto value = vector<2>(corners[indices[j]].at("uv"));
            const auto rgba = colors.at(id(corners[indices[j]].at("vertex")));
            for (unsigned axis = 0; axis < 2; ++axis)
                uv[axis] += value[axis] * weights[j];
            for (unsigned axis = 0; axis < 4; ++axis)
                color[axis] += rgba[axis] * weights[j];
        }
        const auto identity = allocator++;
        Json vertex{{"id", identity}, {"position", inner[i]}};
        if (has_color) {
            for (auto& channel : color)
                channel = std::clamp(channel, 0., 1.);
            vertex["color"] = color;
        }
        edited["vertices"].push_back(std::move(vertex));
        auto corner = corners[i];
        corner["vertex"] = identity;
        corner["uv"] = uv;
        inner_corners.push_back(std::move(corner));
    }
    for (auto& f : edited["faces"])
        if (id(f.at("id")) == face_id)
            f["corners"] = inner_corners;
    for (std::size_t i = 0; i < count; ++i) {
        const auto next = (i + 1) % count;
        auto ring = selected;
        ring["id"] = allocator++;
        ring["corners"] =
            Json::array({corners[i], corners[next], inner_corners[next], inner_corners[i]});
        edited["faces"].push_back(std::move(ring));
    }
    edited["next_id"] = allocator;
    candidate.validate();
    source = std::move(candidate);
    return face_id;
}

std::uint32_t EditableMeshSource::extrude_face(std::uint32_t wanted, double distance) {
    require(std::isfinite(distance) && std::abs(distance) > 1e-5 && std::abs(distance) <= 10000,
            "Mesh extrusion distance must be nonzero and bounded");
    auto candidate = *this;
    auto* source_face = find_face(candidate.document, wanted);
    require(source_face, "Selected Mesh face is missing");
    std::map<std::uint32_t, Vec3> positions;
    std::map<std::uint32_t, Json> colors;
    for (const auto& v : candidate.document.at("vertices")) {
        const auto key = id(v.at("id"));
        positions.emplace(key, vector<3>(v.at("position")));
        if (v.contains("color"))
            colors.emplace(key, v.at("color"));
    }
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
        Json new_vertex{{"id", added}, {"position", p}};
        if (colors.contains(vertex))
            new_vertex["color"] = colors.at(vertex);
        candidate.document["vertices"].push_back(std::move(new_vertex));
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
        faces.push_back(
            {{"id", next++},
             {"corners",
              Json::array({corner(old_vertices[i], {0, 0}), corner(old_vertices[j], {width, 0}),
                           corner(new_vertices[j], {width, std::abs(distance)}),
                           corner(new_vertices[i], {0, std::abs(distance)})})}});
    }
    candidate.document["next_id"] = next;
    candidate.validate();
    document = std::move(candidate.document);
    return cap;
}
void EditableMeshSource::triangulate_faces(std::span<const std::uint32_t> faces) {
    require(!faces.empty() && faces.size() <= face_limit,
            "Mesh triangulation selection is empty or too large");
    validate();
    const std::set<std::uint32_t> selected(faces.begin(), faces.end());
    require(selected.size() == faces.size(),
            "Mesh triangulation selection contains duplicate faces");
    std::size_t found = 0, added_faces = 0, added_corners = 0, old_corners = 0;
    for (const auto& face : document.at("faces")) {
        const auto count = face.at("corners").size();
        old_corners += count;
        if (selected.contains(id(face.at("id")))) {
            ++found;
            added_faces += count - 3;
            added_corners += 2 * count - 6;
        }
    }
    require(found == selected.size(), "Selected Mesh face is missing");
    require(document.at("faces").size() + added_faces <= face_limit &&
                old_corners + added_corners <= corner_limit,
            "Mesh triangulation exceeds face or corner limits");
    const auto first_new = id(document.at("next_id"));
    require(std::uint64_t(first_new) + added_faces <= std::numeric_limits<std::uint32_t>::max(),
            "Mesh element identities exhausted");
    if (added_faces == 0)
        return;
    auto candidate = *this;
    auto next = first_new;
    auto triangles = Json::array();
    for (const auto& face : document.at("faces")) {
        const auto& corners = face.at("corners");
        if (!selected.contains(id(face.at("id"))) || corners.size() == 3) {
            triangles.push_back(face);
            continue;
        }
        for (std::size_t corner = 1; corner + 1 < corners.size(); ++corner) {
            auto triangle = face;
            if (corner != 1)
                triangle["id"] = next++;
            triangle["corners"] = Json::array({corners[0], corners[corner], corners[corner + 1]});
            triangles.push_back(std::move(triangle));
        }
    }
    candidate.document["faces"] = std::move(triangles);
    candidate.document["next_id"] = next;
    candidate.validate();
    *this = std::move(candidate);
}

void EditableMeshSource::sculpt(std::span<const MeshBrushSample> samples, double radius,
                                double strength) {
    sculpt(samples, radius, strength, MeshBrushSymmetry::None);
}
void EditableMeshSource::sculpt(std::span<const MeshBrushSample> samples, double radius,
                                double strength, MeshBrushSymmetry symmetry) {
    require(static_cast<std::uint8_t>(symmetry) <= 7, "Mesh brush symmetry axes are invalid");
    validate_brush(samples, radius);
    require(std::isfinite(strength) && std::abs(strength) <= radius,
            "Mesh sculpt strength exceeds brush radius");
    auto candidate = *this;
    std::map<std::uint32_t, Vec3> positions;
    for (const auto& vertex : document.at("vertices"))
        positions.emplace(id(vertex.at("id")), vector<3>(vertex.at("position")));
    const auto normals = averaged_normals(document, positions);
    std::map<std::uint32_t, Vec3> original_face_normals;
    for (const auto& face : document.at("faces"))
        original_face_normals.emplace(id(face.at("id")), polygon(face, positions).normal);
    for (auto& vertex : candidate.document.at("vertices")) {
        const auto key = id(vertex.at("id"));
        const auto old = positions.at(key);
        const auto influence = brush_influence(old, samples, radius, symmetry);
        if (influence > 0) {
            auto next = old;
            for (unsigned axis = 0; axis < 3; ++axis)
                next[axis] += normals.at(key)[axis] * strength * influence;
            vertex["position"] = next;
        }
    }
    candidate.validate();
    std::map<std::uint32_t, Vec3> changed;
    for (const auto& vertex : candidate.document.at("vertices"))
        changed.emplace(id(vertex.at("id")), vector<3>(vertex.at("position")));
    for (const auto& face : candidate.document.at("faces"))
        require(dot(original_face_normals.at(id(face.at("id"))), polygon(face, changed).normal) >
                    0.2,
                "Sculpt stroke would turn a face inside out");
    document = std::move(candidate.document);
}
void EditableMeshSource::paint_color(std::span<const MeshBrushSample> samples, double radius,
                                     std::array<double, 4> color, double opacity) {
    paint_color(samples, radius, color, opacity, MeshBrushSymmetry::None);
}
void EditableMeshSource::paint_color(std::span<const MeshBrushSample> samples, double radius,
                                     std::array<double, 4> color, double opacity,
                                     MeshBrushSymmetry symmetry) {
    require(static_cast<std::uint8_t>(symmetry) <= 7, "Mesh brush symmetry axes are invalid");
    validate_brush(samples, radius);
    for (const auto channel : color)
        require(std::isfinite(channel) && channel >= 0 && channel <= 1,
                "Mesh paint color must be within [0,1]");
    require(std::isfinite(opacity) && opacity >= 0 && opacity <= 1,
            "Mesh paint opacity must be within [0,1]");
    auto candidate = *this;
    for (auto& vertex : candidate.document.at("vertices")) {
        const auto point = vector<3>(vertex.at("position"));
        const auto amount = opacity * brush_influence(point, samples, radius, symmetry);
        if (amount <= 0)
            continue;
        auto current = vertex.contains("color") ? vertex_color(vertex.at("color"))
                                                : std::array<double, 4>{1, 1, 1, 1};
        for (unsigned channel = 0; channel < 4; ++channel)
            current[channel] += (color[channel] - current[channel]) * amount;
        vertex["color"] = current;
    }
    candidate.validate();
    document = std::move(candidate.document);
}
namespace {
std::vector<Json*> uv_selection(Json& document, std::span<const MeshUvCorner> corners) {
    require(!corners.empty() && corners.size() <= corner_limit, "Select Mesh UV corners");
    std::map<std::uint32_t, Json*> faces;
    for (auto& face : document.at("faces"))
        faces.emplace(id(face.at("id")), &face);
    std::set<MeshUvCorner> unique;
    std::vector<Json*> selected;
    selected.reserve(corners.size());
    for (const auto corner : corners) {
        const auto face = faces.find(corner.face);
        require(face != faces.end() && unique.insert(corner).second &&
                    corner.corner < face->second->at("corners").size(),
                "Selected Mesh UV corner is invalid");
    }
    for (const auto corner : unique)
        selected.push_back(&faces.at(corner.face)->at("corners")[corner.corner]);
    return selected;
}
} // namespace
std::vector<MeshUvCorner> EditableMeshSource::uv_island(std::uint32_t wanted) const {
    validate();
    struct EdgeUse {
        std::uint32_t face;
        std::array<std::array<double, 2>, 2> endpoints;
    };
    std::map<std::pair<std::uint32_t, std::uint32_t>, std::vector<EdgeUse>> edges;
    std::map<std::uint32_t, const Json*> faces;
    for (const auto& face : document.at("faces")) {
        const auto key = id(face.at("id"));
        faces.emplace(key, &face);
        const auto& corners = face.at("corners");
        for (std::size_t i = 0; i < corners.size(); ++i) {
            const auto& a = corners[i];
            const auto& b = corners[(i + 1) % corners.size()];
            const auto va = id(a.at("vertex")), vb = id(b.at("vertex"));
            const auto ua = vector<2>(a.at("uv")), ub = vector<2>(b.at("uv"));
            edges[std::minmax(va, vb)].push_back(
                {key, va < vb ? std::array{ua, ub} : std::array{ub, ua}});
        }
    }
    require(faces.contains(wanted), "Selected Mesh face is missing");
    std::map<std::uint32_t, std::set<std::uint32_t>> adjacent;
    for (const auto& [edge, uses] : edges) {
        (void)edge;
        if (uses.size() == 2 && uses[0].endpoints == uses[1].endpoints) {
            adjacent[uses[0].face].insert(uses[1].face);
            adjacent[uses[1].face].insert(uses[0].face);
        }
    }
    std::set<std::uint32_t> selected{wanted};
    std::vector<std::uint32_t> pending{wanted};
    for (std::size_t i = 0; i < pending.size(); ++i)
        for (const auto next : adjacent[pending[i]])
            if (selected.insert(next).second)
                pending.push_back(next);
    std::vector<MeshUvCorner> result;
    for (const auto face : selected)
        for (std::size_t corner = 0; corner < faces.at(face)->at("corners").size(); ++corner)
            result.push_back({face, corner});
    return result;
}
void EditableMeshSource::transform_uv(std::uint32_t wanted, std::span<const std::size_t> corners,
                                      std::array<double, 2> translation, double angle,
                                      double scale) {
    require(std::isfinite(scale) && scale > 1e-5 && scale <= 10000, "Mesh UV transform is invalid");
    require(!corners.empty() && corners.size() <= 16, "Select Mesh UV corners");
    std::vector<MeshUvCorner> selected;
    selected.reserve(corners.size());
    for (const auto corner : corners)
        selected.push_back({wanted, corner});
    transform_uv(selected, translation, angle, {scale, scale});
}
void EditableMeshSource::transform_uv(std::span<const MeshUvCorner> corners,
                                      std::array<double, 2> translation, double angle,
                                      std::array<double, 2> scale) {
    validate();
    require(std::isfinite(angle), "Mesh UV rotation is invalid");
    for (const auto value : scale)
        require(std::isfinite(value) && std::abs(value) > 1e-5 && std::abs(value) <= 10000,
                "Mesh UV scale is invalid");
    for (const auto value : translation)
        require(std::isfinite(value), "Mesh UV translation is invalid");
    auto candidate = *this;
    const auto selected = uv_selection(candidate.document, corners);
    std::array<double, 2> center{};
    for (const auto corner : selected) {
        const auto uv = vector<2>(corner->at("uv"));
        center[0] += uv[0];
        center[1] += uv[1];
    }
    center[0] /= selected.size();
    center[1] /= selected.size();
    const auto c = std::cos(angle), s = std::sin(angle);
    for (auto* corner : selected) {
        const auto old = vector<2>(corner->at("uv"));
        const auto x = (old[0] - center[0]) * scale[0];
        const auto y = (old[1] - center[1]) * scale[1];
        (*corner)["uv"] = {center[0] + c * x - s * y + translation[0],
                           center[1] + s * x + c * y + translation[1]};
    }
    candidate.validate();
    document.swap(candidate.document);
}
void EditableMeshSource::fit_uv(std::span<const MeshUvCorner> corners, double padding) {
    validate();
    require(std::isfinite(padding) && padding >= 0 && padding <= .49,
            "Mesh UV padding must be within [0,0.49]");
    auto candidate = *this;
    const auto selected = uv_selection(candidate.document, corners);
    std::array<double, 2> minimum{100000, 100000}, maximum{-100000, -100000};
    for (const auto corner : selected) {
        const auto uv = vector<2>(corner->at("uv"));
        for (unsigned axis = 0; axis < 2; ++axis) {
            minimum[axis] = std::min(minimum[axis], uv[axis]);
            maximum[axis] = std::max(maximum[axis], uv[axis]);
        }
    }
    const auto extent = std::max(maximum[0] - minimum[0], maximum[1] - minimum[1]);
    require(extent > 0, "Mesh UV selection has no extent to fit");
    const auto width = 1 - 2 * padding;
    std::array<double, 2> offset{};
    for (unsigned axis = 0; axis < 2; ++axis)
        offset[axis] = (1 - (maximum[axis] - minimum[axis]) / extent * width) / 2;
    for (auto* corner : selected) {
        const auto uv = vector<2>(corner->at("uv"));
        (*corner)["uv"] = {offset[0] + (uv[0] - minimum[0]) / extent * width,
                           offset[1] + (uv[1] - minimum[1]) / extent * width};
    }
    candidate.validate();
    document.swap(candidate.document);
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
