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
        std::cout << "editable Mesh geometry/source tests passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
