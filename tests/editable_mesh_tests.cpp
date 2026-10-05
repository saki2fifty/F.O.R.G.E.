#include <algorithm>
#include <forge/editable_mesh.hpp>
#include <iostream>
#include <stdexcept>

using namespace forge;
namespace {
void require(bool value, const char* why) {
    if (!value)
        throw std::runtime_error(why);
}
template <class F> void rejects(F fn) {
    try {
        fn();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error("Invalid editable Mesh operation was accepted");
}
} // namespace
int main() {
    try {
        const auto identity = AssetId::generate();
        auto source = EditableMeshSource::create_cube(identity);
        auto mesh = source.cook();
        require(mesh.lods.size() == 1 && mesh.lods[0].parts.size() == 1 &&
                    mesh.lods[0].parts[0].indices.size() == 36,
                "Cube did not cook to 12 triangles");
        require(decode_mesh(encode_mesh(mesh)).lods[0].parts[0].indices.size() == 36,
                "Cooked cube did not round trip");
        auto transformed = source;
        const std::array<std::uint32_t, 8> all_vertices{1, 2, 3, 4, 5, 6, 7, 8};
        transformed.transform_vertices(all_vertices, {1, 2, 3}, {0, 90, 0}, {2, 1, 1});
        require(transformed.cook().lods[0].parts[0].indices.size() == 36,
                "Whole-Mesh move/rotate/scale invalidated the cube");
        const auto before_bad_transform = transformed.document;
        rejects([&] { transformed.transform_vertices(all_vertices, {}, {}, {0, 1, 1}); });
        require(transformed.document == before_bad_transform,
                "Rejected Mesh transform changed source");
        const auto old = source.document;
        rejects(
            [&] { source.translate_vertices(std::array<std::uint32_t, 1>{999}, {0.0, 0.0, 1.0}); });
        require(source.document == old, "Rejected vertex edit changed source");
        const auto cap = source.extrude_face(13, 0.75);
        require(cap != 13 && source.document["vertices"].size() == 12 &&
                    source.document["faces"].size() == 10 &&
                    source.cook().lods[0].parts[0].indices.size() == 60,
                "Face extrusion did not produce closed side walls and a cap");
        const auto before_invalid = source.document;
        rejects(
            [&] { source.translate_vertices(std::array<std::uint32_t, 1>{7}, {0.0, 0.0, 10.0}); });
        require(source.document == before_invalid, "Nonplanar edit changed source");
        source.project_face_uv(cap);
        source.transform_uv(cap, std::array<std::size_t, 4>{0, 1, 2, 3}, {0.25, 0.5}, 0.25, 1.5);
        const auto bytes = source.document.dump();
        auto reopened = EditableMeshSource::parse(std::as_bytes(std::span(bytes)));
        require(reopened.document == source.document && reopened.asset() == identity,
                "Editable Mesh save/reopen lost source identities or UVs");
        auto broken = source;
        broken.document["faces"][0]["corners"][0]["vertex"] = 99999;
        rejects([&] { broken.validate(); });
        auto sphere = EditableMeshSource::create_sculpt_sphere(AssetId::generate());
        const auto sphere_mesh = sphere.cook();
        const auto& part = sphere_mesh.lods[0].parts[0];
        require(sphere.document["vertices"].size() == 114 &&
                    sphere.document["faces"].size() == 224 && part.indices.size() == 672,
                "Sculpt sphere did not produce the bounded triangle Mesh");
        const auto& positions = std::get<std::vector<float>>(part.find("POSITION")->values);
        const auto& normals = std::get<std::vector<float>>(part.find("NORMAL")->values);
        for (std::size_t i = 0; i < positions.size(); i += 3)
            require(positions[i] * normals[i] + positions[i + 1] * normals[i + 1] +
                            positions[i + 2] * normals[i + 2] >
                        0,
                    "Sculpt sphere shading normal points inward");
        const MeshBrushSample top{{0, .5, 0}, 1};
        auto sculpted = sphere;
        sculpted.sculpt(std::array{top}, .3, .08);
        require(sculpted.document["vertices"][0]["position"][1].get<double>() > .5 &&
                    sculpted.cook().lods[0].parts[0].indices.size() == 672,
                "Sculpt stroke did not move and cook the sphere");
        auto repeated = sphere;
        repeated.sculpt(std::array{top, top}, .3, .08);
        require(repeated.document == sculpted.document,
                "Repeated stroke samples changed sculpt displacement");
        auto painted = sphere;
        painted.paint_color(std::array{top}, .3, {1, 0, 0, 1}, .8);
        const auto painted_mesh = painted.cook();
        const auto& painted_part = painted_mesh.lods[0].parts[0];
        const auto* color_stream = painted_part.find("COLOR_0");
        require(color_stream && color_stream->components == 4,
                "Paint did not produce a COLOR_0 stream");
        const auto& colors = std::get<std::vector<float>>(color_stream->values);
        require(
            colors.size() == painted_part.vertices * 4 &&
                std::any_of(colors.begin(), colors.end(), [](float value) { return value < .99f; }),
            "Painted color did not survive Mesh cooking");
        auto painted_repeat = sphere;
        painted_repeat.paint_color(std::array{top, top}, .3, {1, 0, 0, 1}, .8);
        require(painted_repeat.document == painted.document,
                "Repeated stroke samples changed paint coverage");
        const auto before_bad_paint = painted.document;
        rejects([&] { painted.paint_color(std::array{top}, .3, {1, 2, 0, 1}, .8); });
        require(painted.document == before_bad_paint, "Rejected paint stroke changed source");
        auto colored_cube = EditableMeshSource::create_cube(AssetId::generate());
        const MeshBrushSample corner{{-.5, -.5, -.5}, 1};
        colored_cube.paint_color(std::array{corner}, .1, {0, 0, 1, 1}, 1);
        const auto old_color =
            colored_cube.document["vertices"][0].value("color", nlohmann::json{});
        require(old_color.is_array(), "Paint did not store source vertex color");
        (void)colored_cube.extrude_face(13, .25);
        require(colored_cube.cook().lods[0].parts[0].find("COLOR_0"),
                "Extrusion discarded painted vertex stream");
        const auto before_bad_sculpt = sculpted.document;
        rejects([&] { sculpted.sculpt(std::array{top}, .3, 1); });
        require(sculpted.document == before_bad_sculpt, "Rejected sculpt stroke changed source");
        std::cout << "editable Mesh geometry/source tests passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
