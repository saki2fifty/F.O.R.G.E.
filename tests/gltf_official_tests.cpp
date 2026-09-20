#include "asset_bytes.hpp"
#include "gltf_native.hpp"
#include <cmath>
#include <forge/transform.hpp>
#include <iostream>
using namespace forge;
using namespace forge::asset_detail;
namespace {
void require(bool value, const char* why) {
    if (!value)
        throw std::runtime_error(why);
}
AffineTransform affine(const std::array<double, 16>& m) {
    AffineTransform result;
    for (unsigned row = 0; row < 3; ++row)
        for (unsigned col = 0; col < 4; ++col)
            result.m[4 * row + col] = m[4 * col + row];
    return result;
}
} // namespace
int main(int argc, char** argv) {
    try {
        require(argc == 2, "Need official fixture root");
        const std::filesystem::path root = argv[1];
        const auto provenance_bytes = read_bytes(root / "provenance.json", 65536);
        const auto provenance = nlohmann::json::parse(provenance_bytes);
        for (const auto& [name, record] : provenance.at("files").items()) {
            const auto bytes = read_bytes(root / name, 1024 * 1024);
            require(bytes.size() == record.at("bytes") &&
                        content_digest(bytes) == record.at("sha256"),
                    "Official sample differs from recorded upstream bytes");
        }
        NativeGltfDocument model(capture_gltf_source(root, "NegativeScaleTest.gltf"));
        for (std::size_t m = 0; m < model.source().document.at("meshes").size(); ++m) {
            const auto cooked = cook_gltf_mesh(model, m);
            const auto bytes = encode_mesh(cooked);
            const auto loaded = decode_mesh(bytes);
            require(encode_mesh(loaded) == bytes && loaded.byte_size() == cooked.byte_size(),
                    "Official geometry changed during cooked runtime loading");
        }
        const auto& hierarchy = model.hierarchy();
        require(hierarchy.nodes.size() == 14 && model.source().images.size() == 2,
                "Official sample hierarchy/images changed");
        std::vector<AffineTransform> world(hierarchy.nodes.size());
        for (auto index : hierarchy.parent_first) {
            const auto& node = hierarchy.nodes[index];
            const auto local = affine(node.matrix);
            world[index] = node.parent == gltf_no_index ? local : world[node.parent] * local;
            if (node.mesh == gltf_no_index)
                continue;
            const auto& mesh = model.source().document.at("meshes").at(node.mesh);
            for (std::size_t p = 0; p < mesh.at("primitives").size(); ++p) {
                const auto primitive = model.primitive(node.mesh, p);
                require(primitive.vertex_count && !primitive.indices.empty(),
                        "Official primitive disappeared");
                const auto normal = normal_transform(world[index]);
                for (auto x : normal.m)
                    require(std::isfinite(x), "Official reflected normal matrix is not finite");
                const auto& positions = primitive.attributes.at("POSITION").values;
                for (std::size_t v = 0; v < positions.size(); v += 3) {
                    const auto position =
                        world[index].point({positions[v], positions[v + 1], positions[v + 2]});
                    for (auto x : position)
                        require(std::isfinite(x), "Official reflected vertex is not finite");
                }
            }
        }
        require(transform_parity(world[4]) == TransformParity::Negative &&
                    transform_parity(world[6]) == TransformParity::Negative &&
                    transform_parity(world[8]) == TransformParity::Negative &&
                    transform_parity(world[9]) == TransformParity::Positive &&
                    transform_parity(world[11]) == TransformParity::Negative &&
                    transform_parity(world[12]) == TransformParity::Positive,
                "Official local/parent/nested negative-scale parity was not preserved");
        std::cout << "Official NegativeScaleTest provenance, native geometry, hierarchy and parity "
                     "passed (CPU only)\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << "\n";
        return 1;
    }
}
