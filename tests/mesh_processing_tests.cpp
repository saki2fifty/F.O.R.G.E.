#include "mesh_processing.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>

using namespace forge;
using namespace forge::asset_detail;
namespace {
void require(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
template <class F> void rejects(F fn, std::string_view expected) {
    try {
        fn();
    } catch (const std::exception& error) {
        if (std::string_view(error.what()).find(expected) != std::string_view::npos)
            return;
        throw std::runtime_error("Unexpected preparation failure: " + std::string(error.what()));
    }
    throw std::runtime_error("Invalid preparation accepted");
}
const std::vector<float>& f(const MeshPart& part, std::string_view name) {
    return std::get<std::vector<float>>(part.find(name)->values);
}
MeshData fixture(bool normal = true, bool morph = true) {
    MeshData mesh;
    MeshPart part;
    part.vertices = 4;
    part.indices = {0, 1, 2, 0, 2, 3};
    part.streams = {
        {"POSITION", 3, std::vector<float>{0, 0, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0}},
        {"TEXCOORD_0", 2, std::vector<float>{0, 0, 1, 0, 0, 1, 1, 1}},
        {"TEXCOORD_1", 2, std::vector<float>{0, 0, 1, 0, 1, 1, 0, 1}},
        {"COLOR_0", 3, std::vector<float>{1, 0, 0, 0, 1, 0, 0, 0, 1, 1, 1, 0}},
        {"_SOURCE", 1, std::vector<std::uint32_t>{0, 1, 2, 3}},
        {"JOINTS_0", 4,
         std::vector<std::uint32_t>{30000000, 0, 0, 0, 30000001, 0, 0, 0, 30000002, 0, 0, 0,
                                    30000003, 0, 0, 0}},
        {"WEIGHTS_0", 4, std::vector<float>{1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0}}};
    if (normal)
        part.streams.push_back(
            {"NORMAL", 3, std::vector<float>{0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1}});
    // Exceed the native Multi API's sixteen-stream limit without losing any channel.
    for (unsigned i = 0; i < 18; ++i)
        part.streams.push_back(
            {"_EXTRA" + std::to_string(i), 1,
             std::vector<float>{float(i), float(i + 1), float(i + 2), float(i + 3)}});
    if (morph) {
        part.morph_targets.push_back(
            {{"POSITION", 3, std::vector<float>{0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0}}});
        mesh.morph_names = {"Lift"};
        mesh.morph_defaults = {0.25f};
    }
    part.bounds = mesh_bounds(part);
    mesh.lods.push_back({1, {std::move(part)}});
    validate_mesh(mesh);
    return mesh;
}
const MeshStream& stream(const std::vector<MeshStream>& streams, std::string_view name) {
    const auto it = std::find_if(streams.begin(), streams.end(),
                                 [&](const auto& s) { return s.semantic == name; });
    require(it != streams.end(), "Prepared stream missing");
    return *it;
}
void preserved_channels(const MeshPart& source, const MeshPart& result) {
    const auto& mapping = std::get<std::vector<std::uint32_t>>(result.find("_SOURCE")->values);
    for (std::size_t i = 0; i < result.indices.size(); ++i)
        require(mapping[result.indices[i]] == source.indices[i],
                "Preparation changed primitive order/corner correspondence");
    auto compare = [&](const MeshStream& src, const MeshStream& dst) {
        require(src.components == dst.components && src.values.index() == dst.values.index(),
                "Channel format changed");
        std::visit(
            [&](const auto& input) {
                const auto& output = std::get<std::decay_t<decltype(input)>>(dst.values);
                for (std::size_t v = 0; v < result.vertices; ++v)
                    for (unsigned c = 0; c < src.components; ++c)
                        require(output[v * src.components + c] ==
                                    input[mapping[v] * src.components + c],
                                "Channel or morph values were not remapped with positions");
            },
            src.values);
    };
    for (const auto& channel : source.streams)
        compare(channel, *result.find(channel.semantic));
    for (std::size_t t = 0; t < source.morph_targets.size(); ++t)
        for (const auto& channel : source.morph_targets[t])
            compare(channel, stream(result.morph_targets[t], channel.semantic));
}
void flat_target_normals(const MeshPart& part) {
    const auto& position = f(part, "POSITION");
    const auto& normal = f(part, "NORMAL");
    const auto& delta =
        std::get<std::vector<float>>(stream(part.morph_targets[0], "POSITION").values);
    const auto& normal_delta =
        std::get<std::vector<float>>(stream(part.morph_targets[0], "NORMAL").values);
    for (std::size_t face = 0; face < part.indices.size(); face += 3) {
        double p[3][3], a[3], b[3], n[3];
        for (unsigned c = 0; c < 3; ++c)
            for (unsigned axis = 0; axis < 3; ++axis) {
                const auto id = part.indices[face + c] * 3 + axis;
                p[c][axis] = double(position[id]) + delta[id];
            }
        for (unsigned axis = 0; axis < 3; ++axis) {
            a[axis] = p[1][axis] - p[0][axis];
            b[axis] = p[2][axis] - p[0][axis];
        }
        for (unsigned axis = 0; axis < 3; ++axis)
            n[axis] = a[(axis + 1) % 3] * b[(axis + 2) % 3] - a[(axis + 2) % 3] * b[(axis + 1) % 3];
        const auto length = std::hypot(n[0], n[1], n[2]);
        for (unsigned c = 0; c < 3; ++c)
            for (unsigned axis = 0; axis < 3; ++axis) {
                const auto id = part.indices[face + c] * 3 + axis;
                require(std::abs(normal[id] + normal_delta[id] - n[axis] / length) < 0.000001,
                        "Morph endpoint normal differs from its deformed flat face");
            }
    }
}
} // namespace
int main() {
    try {
        const auto input = fixture();
        const auto before = encode_mesh(input);
        const auto result = process_mesh(input);
        const auto& part = result.mesh.lods[0].parts[0];
        require(part.vertices == 6, "Mirrored UV seam was not split");
        preserved_channels(input.lods[0].parts[0], part);
        bool positive = false, negative = false;
        for (std::size_t i = 0; i < part.vertices; ++i) {
            positive |= f(part, "TANGENT")[i * 4 + 3] == 1;
            negative |= f(part, "TANGENT")[i * 4 + 3] == -1;
        }
        require(positive && negative && part.morph_targets[0].size() == 2,
                "Generated tangent signs or morph tangent deltas lost");
        require(result.mesh.morph_names == input.morph_names &&
                    result.mesh.morph_defaults == input.morph_defaults,
                "Morph labels/defaults changed");
        require(encode_mesh(input) == before &&
                    encode_mesh(process_mesh(input).mesh) == encode_mesh(result.mesh),
                "Preparation mutated source or is not repeatable");
        auto flat_source = fixture(false);
        auto flat = process_mesh(flat_source);
        preserved_channels(flat_source.lods[0].parts[0], flat.mesh.lods[0].parts[0]);
        flat_target_normals(flat.mesh.lods[0].parts[0]);
        require(flat.mesh.lods[0].parts[0].find("NORMAL") &&
                    flat.mesh.lods[0].parts[0].find("TANGENT"),
                "Missing directions were not generated");
        MeshProcessingOptions uv1;
        uv1.tangent_uv_sets[0] = 1;
        auto alternate = process_mesh(fixture(true, false), uv1);
        require(alternate.mesh.lods[0].parts[0].vertices == 4, "Selected UV set was ignored");
        for (std::size_t i = 0; i < 4; ++i)
            require(f(alternate.mesh.lods[0].parts[0], "TANGENT")[i * 4 + 3] == 1,
                    "Selected normal UV changed orientation");
        for (float scale : {1e-25f, 1e25f}) {
            auto scaled = fixture(true, false);
            auto& p = scaled.lods[0].parts[0];
            for (auto& s : p.streams)
                if (s.semantic == "POSITION" || s.semantic.starts_with("TEXCOORD_"))
                    for (auto& v : std::get<std::vector<float>>(s.values))
                        v *= scale;
            p.bounds = mesh_bounds(p);
            const auto conditioned = process_mesh(scaled, uv1);
            const auto& ts = f(conditioned.mesh.lods[0].parts[0], "TANGENT");
            for (std::size_t i = 0; i < ts.size(); i += 4)
                require(std::abs(ts[i] - 1) < 0.000001 && ts[i + 3] == 1,
                        "Native tangent preparation lost scale/UV invariance");
        }
        MeshProcessingOptions preserve;
        preserve.normals = preserve.tangents = MeshDirections::Preserve;
        preserve.weld_exact = preserve.optimize_vertex_fetch = false;
        require(encode_mesh(process_mesh(input, preserve).mesh) == before,
                "Preserve recipe changed source streams");
        auto recalc = preserve;
        recalc.normals = recalc.tangents = MeshDirections::Recalculate;
        flat_target_normals(process_mesh(input, recalc).mesh.lods[0].parts[0]);
        // Identical base geometry with distinct morph values must never weld.
        MeshData distinct;
        MeshPart points;
        points.topology = MeshTopology::Points;
        points.vertices = 3;
        points.indices = {0, 1, 2};
        points.streams = {{"POSITION", 3, std::vector<float>(9, 0)}};
        points.morph_targets = {{{"POSITION", 3, std::vector<float>{0, 0, 0, 1, 0, 0, 1, 0, 0}}}};
        points.bounds = mesh_bounds(points);
        distinct.morph_names = {"Move"};
        distinct.morph_defaults = {0};
        distinct.lods = {{1, {points}}};
        const auto welded = process_mesh(distinct).mesh;
        require(welded.lods[0].parts[0].vertices == 2 &&
                    welded.lods[0].parts[0].indices == std::vector<std::uint32_t>{0, 1, 1},
                "Exact welding ignored morph identity or lost point order");
        auto missing_uv = input;
        std::erase_if(missing_uv.lods[0].parts[0].streams,
                      [](const auto& s) { return s.semantic.starts_with("TEXCOORD_"); });
        require(!process_mesh(missing_uv).mesh.lods[0].parts[0].find("TANGENT"),
                "Absent UV produced fictitious tangent stream");
        rejects([&] { (void)process_mesh(missing_uv, uv1); }, "selected TEXCOORD_1");
        auto collapsed = fixture(false, false);
        auto& cp = collapsed.lods[0].parts[0];
        for (auto& s : cp.streams)
            if (s.semantic == "POSITION")
                std::fill(std::get<std::vector<float>>(s.values).begin(),
                          std::get<std::vector<float>>(s.values).end(), 0.f);
        cp.bounds = mesh_bounds(cp);
        require(!process_mesh(collapsed).diagnostics.empty(),
                "Collapsed geometry fallback was not diagnosed");
        MeshLimits small;
        small.vertices = 4;
        rejects([&] { (void)process_mesh(input, {}, small); }, "corner preparation");
        auto order = preserve;
        order.order_independent_material_slots = {0};
        validate_mesh(process_mesh(input, order).mesh);
        order.order_independent_material_slots = {1};
        rejects([&] { (void)process_mesh(input, order); }, "material slot");
        std::stop_source stop;
        stop.request_stop();
        rejects([&] { (void)process_mesh(input, {}, {}, stop.get_token()); }, "cancelled");
        require(encode_mesh(input) == before, "Failed preparation mutated caller mesh");
        std::cout
            << "Mesh directions, morphs, mirrored seams, coherent remapping and budgets passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
