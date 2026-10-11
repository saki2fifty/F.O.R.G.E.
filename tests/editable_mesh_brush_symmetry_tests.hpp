#pragma once
namespace {
std::vector<MeshBrushSample> reflected_samples(std::span<const MeshBrushSample> original,
                                               unsigned axes) {
    std::vector<MeshBrushSample> out;
    for (const auto sample : original)
        for (unsigned reflection = 0; reflection < 8; ++reflection)
            if ((reflection & ~axes) == 0) {
                auto reflected = sample;
                for (unsigned axis = 0; axis < 3; ++axis)
                    if (reflection & (1u << axis))
                        reflected.point[axis] = -reflected.point[axis];
                out.push_back(reflected);
            }
    return out;
}
void brush_symmetry_tests() {
    auto source = EditableMeshSource::create_sculpt_sphere(AssetId::generate());
    source.document["extension"] = {{"keep", 42}};
    const auto original = source.document;
    const std::array samples{MeshBrushSample{{.45, .1, -.2}, .8},
                             MeshBrushSample{{.2, .45, .1}, .35}};
    for (unsigned axes = 0; axes < 8; ++axes) {
        const auto expanded = reflected_samples(samples, axes);
        const auto symmetry = static_cast<MeshBrushSymmetry>(axes);
        for (double strength : {.015, -.015}) {
            auto authored = source, oracle = source;
            authored.sculpt(samples, .35, strength, symmetry);
            oracle.sculpt(expanded, .35, strength);
            require(authored.document == oracle.document,
                    "Folded sculpt differs from explicit reflection oracle");
            require(authored.document.at("faces") == original.at("faces") &&
                        authored.document.at("next_id") == original.at("next_id") &&
                        authored.document.at("extension") == original.at("extension"),
                    "Symmetric sculpt changed topology/UV/identity/extensions");
        }
        auto authored = source, oracle = source;
        authored.paint_color(samples, .35, {.2, .4, .7, .8}, .3, symmetry);
        oracle.paint_color(expanded, .35, {.2, .4, .7, .8}, .3);
        require(authored.document == oracle.document,
                "Folded paint differs from explicit reflection oracle");
        for (std::size_t i = 0; i < authored.document.at("vertices").size(); ++i)
            require(authored.document["vertices"][i]["position"] ==
                        original["vertices"][i]["position"],
                    "Symmetric paint moved geometry");
        require(authored.document.at("faces") == original.at("faces"),
                "Paint changed corner UVs/topology");
        require(authored.cook().lods[0].parts[0].find("COLOR_0"),
                "Symmetric paint did not cook vertex color");
    }
    // Cube quads have a reflection-symmetric averaged normal field; all-axis
    // corner influence must preserve exact paired geometry for either sign.
    for (double strength : {.02, -.02}) {
        auto cube = EditableMeshSource::create_cube(AssetId::generate());
        const auto before = cube.document;
        const auto corner = before.at("vertices")[0].at("position").get<std::array<double, 3>>();
        cube.sculpt(std::array{MeshBrushSample{corner, 1}}, .2, strength,
                    MeshBrushSymmetry::X | MeshBrushSymmetry::Y | MeshBrushSymmetry::Z);
        for (std::size_t i = 0; i < 8; ++i) {
            const auto p = before["vertices"][i]["position"].get<std::array<double, 3>>();
            const auto q = cube.document["vertices"][i]["position"].get<std::array<double, 3>>();
            for (unsigned axis = 0; axis < 3; ++axis)
                require(std::abs(q[axis] - (p[axis] + std::copysign(1., p[axis]) * strength /
                                                          std::sqrt(3.))) < 1e-12,
                        "Symmetric topology/normals failed signed all-axis sculpt");
        }
        require(cube.document.at("faces") == before.at("faces"),
                "Symmetry changed Cube topology/UV");
    }
    const std::array on_plane{MeshBrushSample{{0, .5, 0}, .7}};
    auto once = source, mirrored = source;
    once.sculpt(on_plane, .3, .02);
    mirrored.sculpt(on_plane, .3, .02, MeshBrushSymmetry::X);
    require(once.document == mirrored.document, "On-plane reflection doubled sculpt strength");
    once = source;
    mirrored = source;
    once.paint_color(on_plane, .3, {1, 0, 0, 1}, .4);
    mirrored.paint_color(on_plane, .3, {1, 0, 0, 1}, .4, MeshBrushSymmetry::X);
    require(once.document == mirrored.document, "On-plane reflection doubled paint opacity");
    const std::array origin{MeshBrushSample{{0, 0, 0}, 1}};
    once = source;
    mirrored = source;
    once.paint_color(origin, .8, {1, 0, 0, 1}, .3);
    mirrored.paint_color(origin, .8, {1, 0, 0, 1}, .3,
                         MeshBrushSymmetry::X | MeshBrushSymmetry::Y | MeshBrushSymmetry::Z);
    require(once.document == mirrored.document,
            "Coincident all-axis reflections accumulated opacity");
    for (unsigned invalid : {8u, 255u}) {
        rejects(
            [&] { source.sculpt(samples, .35, .015, static_cast<MeshBrushSymmetry>(invalid)); });
        rejects([&] {
            source.paint_color(samples, .35, {1, 0, 0, 1}, .3,
                               static_cast<MeshBrushSymmetry>(invalid));
        });
        require(source.document == original, "Invalid symmetry mutated source");
    }
    std::vector<MeshBrushSample> full(256, samples.front());
    once = source;
    mirrored = source;
    once.paint_color(std::array{samples.front()}, .35, {1, 0, 0, 1}, .3,
                     MeshBrushSymmetry::X | MeshBrushSymmetry::Y | MeshBrushSymmetry::Z);
    mirrored.paint_color(full, .35, {1, 0, 0, 1}, .3,
                         MeshBrushSymmetry::X | MeshBrushSymmetry::Y | MeshBrushSymmetry::Z);
    require(once.document == mirrored.document, "Maximum stroke count changed mirror influence");
    full.push_back(samples.front());
    rejects([&] { source.sculpt(full, .35, .015, MeshBrushSymmetry::X); });
    rejects([&] { source.paint_color(full, .35, {1, 0, 0, 1}, .3, MeshBrushSymmetry::X); });
    require(source.document == original, "Oversized symmetry stroke changed source");
}
} // namespace
