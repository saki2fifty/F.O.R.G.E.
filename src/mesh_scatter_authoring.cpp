#include "mesh_scatter_authoring.hpp"
#include "scene_draft.hpp"
#include "spatial_document.hpp"
#include <algorithm>
#include <cmath>
#include <forge/authoring.hpp>
#include <forge/primitive_catalog.hpp>
#include <map>
#include <set>
namespace forge::detail {
namespace {
void invalid(const char* message) { throw AuthoringError("invalid_arguments", message); }
void range(double value, double low, double high) {
    if (!std::isfinite(value) || value < low || value > high)
        invalid("Foliage placement value is outside its supported range");
}
} // namespace
std::vector<MeshScatterPlacement> mesh_scatter_layout(const MeshScatterSettings& s) {
    for (double v : s.center)
        range(v, -1e12, 1e12);
    range(s.radius, .01, 1000);
    range(s.spacing, 0, 2000);
    range(s.scale_min, .01, 100);
    range(s.scale_max, s.scale_min, 100);
    range(s.yaw_min, -360, 360);
    range(s.yaw_max, s.yaw_min, 360);
    if (s.count < 1 || s.count > mesh_scatter_limit)
        invalid("Foliage placement count must be 1 to 512");
    // Explicit unsigned arithmetic makes the sequence independent of library RNGs.
    auto state = s.seed;
    auto random = [&] {
        state = state * 1664525u + 1013904223u;
        return double(state) / 4294967296.0;
    };
    std::vector<MeshScatterPlacement> result;
    result.reserve(s.count);
    using Cell = std::pair<std::int64_t, std::int64_t>;
    std::map<Cell, std::vector<std::size_t>> cells;
    // Very small spacing needs no index finer than this: neighboring cells still
    // contain every point closer than the requested distance.
    const double cell_size = std::max(s.spacing, .000001);
    const auto cell = [&](Double2 p) {
        return Cell{std::int64_t(std::floor(p[0] / cell_size)),
                    std::int64_t(std::floor(p[1] / cell_size))};
    };
    for (unsigned attempt = 0; attempt < mesh_scatter_limit * 128 && result.size() < s.count;
         ++attempt) {
        const Double2 offset{(2 * random() - 1) * s.radius, (2 * random() - 1) * s.radius};
        if (offset[0] * offset[0] + offset[1] * offset[1] > s.radius * s.radius)
            continue;
        const Double2 world{s.center[0] + offset[0], s.center[2] + offset[1]};
        const Double2 relative{world[0] - s.center[0], world[1] - s.center[2]};
        if (std::hypot(relative[0], relative[1]) > s.radius)
            continue;
        const auto key = cell(relative);
        bool overlaps = false;
        if (s.spacing > 0)
            // Index relative coordinates to keep cell keys bounded. Two cells also
            // cover subtraction rounding; distances use published double-world origins.
            for (std::int64_t x = key.first - 2; x <= key.first + 2 && !overlaps; ++x)
                for (std::int64_t z = key.second - 2; z <= key.second + 2 && !overlaps; ++z) {
                    const auto found = cells.find({x, z});
                    if (found == cells.end())
                        continue;
                    for (auto i : found->second) {
                        const auto& p = result[i].offset;
                        const double dx = (s.center[0] + p[0]) - world[0];
                        const double dz = (s.center[2] + p[1]) - world[1];
                        if (std::hypot(dx, dz) < s.spacing) {
                            overlaps = true;
                            break;
                        }
                    }
                }
        if (overlaps)
            continue;
        cells[key].push_back(result.size());
        result.push_back({offset, s.scale_min + (s.scale_max - s.scale_min) * random(),
                          s.yaw_min + (s.yaw_max - s.yaw_min) * random()});
    }
    if (result.size() != s.count)
        throw AuthoringError("placement_exhausted",
                             "Could not fit the requested foliage count within 65536 attempts. "
                             "Increase radius, reduce spacing/count, or choose another seed. "
                             "No objects were created.");
    return result;
}
Json mesh_scatter_arguments(const std::string& source, const MeshScatterSettings& s,
                            const std::string& name) {
    return {{"entity", source},
            {"name", name},
            {"center", {{"x", s.center[0]}, {"y", s.center[1]}, {"z", s.center[2]}}},
            {"radius", s.radius},
            {"spacing", s.spacing},
            {"count", s.count},
            {"seed", s.seed},
            {"scale_min", s.scale_min},
            {"scale_max", s.scale_max},
            {"yaw_min", s.yaw_min},
            {"yaw_max", s.yaw_max}};
}
MeshScatterSettings mesh_scatter_settings(const Json& a) {
    const auto& center = a.at("center");
    return MeshScatterSettings{{center.at("x"), center.at("y"), center.at("z")},
                               a.at("radius"),
                               a.at("spacing"),
                               a.at("scale_min"),
                               a.at("scale_max"),
                               a.at("yaw_min"),
                               a.at("yaw_max"),
                               a.at("count"),
                               a.at("seed")};
}
Json scatter_mesh(SceneDraft& scene, const std::string& source, const Json& a) {
    const auto settings = mesh_scatter_settings(a);
    const auto name = a.at("name").get<std::string>();
    if (name.find_first_not_of(" \t\r\n") == std::string::npos)
        invalid("Foliage group name must not be blank");
    auto doc = scene.document();
    if (doc.at("entities").size() + settings.count + 1 > 10000)
        throw AuthoringError("limit_exceeded",
                             "Foliage placement would exceed 10000 Scene entities");
    const auto view = scene.effective_document();
    const Json* prototype = nullptr;
    for (const auto& row : view.at("entities"))
        if (row.at("id") == source)
            prototype = &row;
    if (!prototype || prototype->value("prefab", false) ||
        !prototype->at("components").contains("forge.mesh_renderer"))
        invalid("Choose a Scene entity with a Mesh Renderer as the foliage template");
    const auto& renderer = prototype->at("components").at("forge.mesh_renderer");
    if (!renderer.contains("mesh") || !renderer.at("mesh").is_string() ||
        !AssetId::parse(renderer.at("mesh").get<std::string>()))
        invalid("The foliage template needs a nonempty Mesh reference");
    const auto local = read_local(prototype->at("components"));
    const auto placements = mesh_scatter_layout(settings);
    std::vector<double> heights;
    if (a.contains("world_heights")) {
        const auto& values = a.at("world_heights");
        if (!values.is_array() || values.size() != placements.size())
            invalid("Resolved world heights must match the generated placement count");
        heights.reserve(values.size());
        for (const auto& value : values) {
            if (!value.is_number())
                invalid("Resolved world heights must be numeric");
            const auto height = value.get<double>();
            range(height, -1e12, 1e12);
            heights.push_back(height);
        }
    }
    std::set<std::string> occupied;
    for (const auto& row : doc.at("entities"))
        occupied.insert(row.at("id").get<std::string>());
    auto fresh = [&] {
        std::string id;
        do {
            id = EntityId::generate().str();
        } while (!occupied.insert(id).second);
        return id;
    };
    const auto group = fresh();
    Json root = {{"id", group},
                 {"name", name},
                 {"spatial", {{"mode", "follow_structure"}}},
                 {"components", {{"forge.primitive", {{"kind", no_primitive}}}}}};
    LocalTransform group_transform;
    group_transform.translation = {settings.center[0], settings.center[1], settings.center[2]};
    write_local(root, {}, group_transform, TransformChannel::All);
    doc["entities"].push_back(std::move(root));
    Json created = Json::array();
    for (std::size_t i = 0; i < placements.size(); ++i) {
        const auto& p = placements[i];
        const auto id = fresh();
        Json row = {{"id", id},
                    {"name", name + " " + std::to_string(i + 1)},
                    {"parent", group},
                    {"spatial", {{"mode", "follow_structure"}}},
                    {"components", {{"forge.mesh_renderer", renderer}}}};
        auto transform = local;
        transform.translation = {p.offset[0], heights.empty() ? 0 : heights[i] - settings.center[1],
                                 p.offset[1]};
        // Yaw about +Y precedes the template local rotation. The existing
        // rotation owner normalizes the result; position/gameplay are not copied.
        const auto yaw = rotation_about_axis({0, 1, 0}, p.yaw);
        const auto& q = local.rotation;
        transform.rotation = normalized({yaw.w * q.x + yaw.y * q.z, yaw.w * q.y + yaw.y * q.w,
                                         yaw.w * q.z - yaw.y * q.x, yaw.w * q.w - yaw.y * q.y});
        transform.scale = checked_local_scale(
            {local.scale.x * p.scale, local.scale.y * p.scale, local.scale.z * p.scale});
        write_local(row, {}, transform, TransformChannel::All);
        doc["entities"].push_back(std::move(row));
        created.push_back(id);
    }
    scene.edit(doc);
    return {{"operation", "mesh.scatter"}, {"selected", group}, {"created", created}};
}
} // namespace forge::detail
