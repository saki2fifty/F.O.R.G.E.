#pragma once
#include <cmath>
#include <map>
namespace {
void mesh_shading_tests() {
    auto cube = EditableMeshSource::create_cube(AssetId::generate());
    cube.document["extension"] = {{"test", 42}};
    cube.document["vertices"][0]["color"] = {.1, .2, .3, 1.};
    cube.document["faces"][0]["corners"][0]["extension"] = {{"test", 7}};
    const auto original = cube.document;
    cube.set_smooth_shading(false);
    require(cube.document == original, "Default flat shading must be a no-op");
    const auto flat = cube.cook();
    cube.set_smooth_shading(true);
    auto unchanged = cube.document;
    unchanged.erase("smooth");
    require(unchanged == original, "Shading changed source geometry, IDs or attributes");
    const auto smooth = cube.cook();
    const auto& a = flat.lods[0].parts[0];
    const auto& b = smooth.lods[0].parts[0];
    require(a.indices == b.indices, "Shading changed cooked topology");
    for (const auto& stream : a.streams) {
        require(b.find(stream.semantic), "Shading lost cooked stream");
        if (stream.semantic != "NORMAL")
            require(stream.values == b.find(stream.semantic)->values,
                    "Shading changed nonnormal stream");
    }
    const auto& pos = std::get<std::vector<float>>(b.find("POSITION")->values);
    const auto& normals = std::get<std::vector<float>>(b.find("NORMAL")->values);
    const auto& flat_normals = std::get<std::vector<float>>(a.find("NORMAL")->values);
    for (std::size_t i = 0; i < pos.size(); i += 3) {
        double flat_sum = 0, unit = 0;
        for (unsigned axis = 0; axis < 3; ++axis) {
            const auto expected = std::copysign(1 / std::sqrt(3.), pos[i + axis]);
            require(std::abs(normals[i + axis] - expected) < 1e-6,
                    "Cube averaged normal differs from diagonal analytic normal");
            flat_sum += std::abs(flat_normals[i + axis]);
            unit += double(normals[i + axis]) * normals[i + axis];
        }
        require(std::abs(flat_sum - 1) < 1e-6 && std::abs(unit - 1) < 1e-6,
                "Flat or smooth normal is not unit");
    }
    const auto once = cube.document;
    cube.set_smooth_shading(true);
    require(cube.document == once, "Same shading is not a no-op");
    cube.set_smooth_shading(false);
    require(cube.cook().lods[0].parts[0].find("NORMAL")->values == a.find("NORMAL")->values,
            "Flat recovery changed normals");
    // Independent geometric normal oracle, using source corners and long double.
    auto sphere = EditableMeshSource::create_sculpt_sphere(AssetId::generate());
    const auto geometry = sphere.document;
    const auto spherical = sphere.cook();
    sphere.set_smooth_shading(false);
    const auto faceted = sphere.cook();
    using V = std::array<long double, 3>;
    std::map<std::uint32_t, V> points, sums;
    std::vector<V> face_normals;
    for (const auto& vertex : geometry.at("vertices")) {
        const auto p = vertex.at("position").get<std::array<double, 3>>();
        points[vertex.at("id").get<std::uint32_t>()] = {p[0], p[1], p[2]};
    }
    for (const auto& face : geometry.at("faces")) {
        const auto& corners = face.at("corners");
        const auto a = points.at(corners[0].at("vertex").get<std::uint32_t>()),
                   b = points.at(corners[1].at("vertex").get<std::uint32_t>()),
                   c = points.at(corners[2].at("vertex").get<std::uint32_t>());
        const V u{b[0] - a[0], b[1] - a[1], b[2] - a[2]}, v{c[0] - a[0], c[1] - a[1], c[2] - a[2]};
        V n{u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0]};
        const auto len = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
        for (auto& component : n)
            component /= len;
        face_normals.push_back(n);
        for (const auto& corner : corners)
            for (unsigned axis = 0; axis < 3; ++axis)
                sums[corner.at("vertex").get<std::uint32_t>()][axis] += n[axis];
    }
    const auto& sn =
        std::get<std::vector<float>>(spherical.lods[0].parts[0].find("NORMAL")->values);
    const auto& fn = std::get<std::vector<float>>(faceted.lods[0].parts[0].find("NORMAL")->values);
    std::size_t offset = 0, index = 0;
    for (const auto& face : geometry.at("faces")) {
        for (const auto& corner : face.at("corners")) {
            auto n = sums.at(corner.at("vertex").get<std::uint32_t>());
            const auto len = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
            for (unsigned axis = 0; axis < 3; ++axis) {
                require(std::abs(sn[offset + axis] - n[axis] / len) < 1e-6L,
                        "Sphere averaged normal differs from geometric oracle");
                require(std::abs(fn[offset + axis] - face_normals[index][axis]) < 1e-6L,
                        "Sphere flat normal differs from geometric oracle");
            }
            offset += 3;
        }
        ++index;
    }
    require(sn != fn, "Sphere shading comparison has identical normals");
    for (const auto& stream : spherical.lods[0].parts[0].streams)
        if (stream.semantic != "NORMAL")
            require(stream.values == faceted.lods[0].parts[0].find(stream.semantic)->values,
                    "Sphere shading changed nonnormal stream");
    // Two opposite triangles share all vertices and valid opposite edge winding.
    // Their incident normals cancel; flat cooking is valid, averaging is undefined.
    auto opposite = EditableMeshSource::create_cube(AssetId::generate());
    opposite.document["vertices"] = nlohmann::json::array({{{"id", 1}, {"position", {0, 0, 0}}},
                                                           {{"id", 2}, {"position", {1, 0, 0}}},
                                                           {{"id", 3}, {"position", {0, 1, 0}}}});
    const auto corner = [](unsigned v) { return nlohmann::json{{"vertex", v}, {"uv", {0, 0}}}; };
    opposite.document["faces"] =
        nlohmann::json::array({{{"id", 4}, {"corners", {corner(1), corner(2), corner(3)}}},
                               {{"id", 5}, {"corners", {corner(1), corner(3), corner(2)}}}});
    opposite.document["next_id"] = 6;
    opposite.validate();
    (void)opposite.cook();
    const auto safe = opposite.document;
    rejects([&] { opposite.set_smooth_shading(true); });
    require(opposite.document == safe, "Undefined smooth normal changed source");
    opposite.document["smooth"] = true;
    rejects([&] { (void)opposite.cook(); });
    opposite.set_smooth_shading(false);
    (void)opposite.cook();
}
} // namespace
