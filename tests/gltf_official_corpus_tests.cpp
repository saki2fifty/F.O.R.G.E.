#include "asset_bytes.hpp"
#include "gltf_native.hpp"
#include "gltf_scene.hpp"
#include "gltf_surfaces.hpp"
#include "model_importer.hpp"
#include "pbr_material.hpp"
#include "texture_import.hpp"
#include <chrono>
#include <iostream>
#include <set>
using namespace forge;
using namespace forge::asset_detail;
using Json = nlohmann::json;
namespace {
void check(bool ok, const char* why) {
    if (!ok)
        throw std::runtime_error(why);
}
Json inspect(const std::filesystem::path& root, const Json& fixture) {
    const auto started = std::chrono::steady_clock::now();
    NativeGltfDocument model(capture_gltf_source(root, fixture.at("source").get<std::string>(),
                                                 model_cook_extensions()));
    const auto& source = model.source();
    const auto& document = source.document;
    std::size_t parts = 0, vertices = 0, morphs = 0, cooked_bytes = 0, pixels = 0, tracks = 0;
    const auto meshes = document.value("meshes", Json::array()).size();
    for (std::size_t m = 0; m < meshes; ++m) {
        auto mesh = cook_gltf_mesh(model, m);
        const auto bytes = encode_mesh(mesh);
        check(encode_mesh(decode_mesh(bytes)) == bytes, "Official mesh cooked roundtrip differs");
        cooked_bytes += bytes.size();
        for (const auto& part : mesh.lods.front().parts) {
            ++parts;
            vertices += part.vertices;
            morphs += part.morph_targets.size();
        }
    }
    for (const auto& node : model.hierarchy().nodes) {
        if (node.skin == gltf_no_index || node.mesh == gltf_no_index)
            continue;
        const auto& skin = model.hierarchy().skins.at(node.skin);
        check(model.inverse_bind_matrices(node.skin).size() == skin.joints.size(),
              "Official skin bind count differs");
        for (std::size_t p = 0; p < document.at("meshes")[node.mesh].at("primitives").size(); ++p) {
            const auto primitive = model.primitive(node.mesh, p);
            const auto influences = prepare_gltf_skin_influences(primitive, skin.joints.size(),
                                                                 ExcessSkinInfluences::Reject);
            check(influences.vertices.size() == primitive.vertex_count &&
                      !influences.palette.empty(),
                  "Official skin influences disappeared");
        }
    }
    const auto materials = document.value("materials", Json::array()).size();
    std::set<std::pair<std::size_t, TextureSemantic>> decoded_images;
    for (std::size_t m = 0; m < materials; ++m) {
        const auto material = cook_gltf_material(model, m);
        check(decode_material(encode_material(material)) == material,
              "Official material cooked roundtrip differs");
        (void)prepare_pbr_material(material);
        for (const auto& binding : gltf_texture_bindings(source, m)) {
            if (!decoded_images.emplace(binding.image, binding.semantic).second)
                continue;
            const auto& image = model.encoded_images().at(binding.image);
            TextureImportSettings settings;
            settings.semantic = binding.semantic;
            settings.srgb = binding.semantic == TextureSemantic::Color;
            const auto hint = image.mime_type == "image/jpeg" ? "official.jpg" : "official.png";
            auto texture = import_texture_image(image.encoded.bytes(), hint, settings);
            check(texture.width && texture.height && !texture.subresources.empty(),
                  "Official material image did not decode");
            const auto bytes = encode_texture(texture);
            check(encode_texture(decode_texture(bytes)) == bytes,
                  "Official texture cooked roundtrip differs");
            pixels += std::size_t(texture.width) * texture.height;
            cooked_bytes += bytes.size();
        }
    }
    const auto clips = document.value("animations", Json::array()).size();
    for (std::size_t a = 0; a < clips; ++a) {
        const auto clip = model.animation(a);
        check(!clip.tracks.empty() && clip.duration >= 0,
              "Official animation has no admitted tracks");
        tracks += clip.tracks.size();
    }
    const auto scene = gltf_scene_metadata(model.scene_source());
    const auto elapsed =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started)
            .count();
    return {{"fixture", fixture.at("name")},
            {"meshes", meshes},
            {"parts", parts},
            {"vertices", vertices},
            {"morph_targets", morphs},
            {"materials", materials},
            {"image_variants", decoded_images.size()},
            {"pixels", pixels},
            {"skins", model.hierarchy().skins.size()},
            {"animation_clips", clips},
            {"animation_tracks", tracks},
            {"cameras", scene.cameras.size()},
            {"lights", scene.lights.size()},
            {"cooked_bytes", cooked_bytes},
            {"cpu_ms", elapsed}};
}
} // namespace
int main(int argc, char** argv) {
    try {
        check(argc == 2, "Need official corpus root");
        const auto root = std::filesystem::absolute(argv[1]);
        const auto bytes = read_bytes(root / "provenance.json", 1024 * 1024);
        const auto provenance = Json::parse(bytes.begin(), bytes.end());
        check(provenance.at("revision") == "c6a6bd13ab2b3c685c7903d03561b8a9392f38b8" &&
                  provenance.at("modified") == false && provenance.at("fixtures").size() == 18,
              "Official corpus revision/set changed without review");
        for (const auto& [name, record] : provenance.at("files").items()) {
            const auto file = read_bytes(ProjectPaths(root).resolve(name), 16 * 1024 * 1024);
            check(file.size() == record.at("bytes").get<std::uint64_t>() &&
                      content_digest(file) == record.at("sha256").get<std::string>(),
                  "Official fixture bytes differ from recorded source");
        }
        unsigned failed = 0;
        for (const auto& fixture : provenance.at("fixtures")) {
            try {
                std::cout << inspect(root, fixture).dump() << '\n';
            } catch (const std::exception& e) {
                std::cerr << fixture.at("name") << ": " << e.what() << '\n';
                ++failed;
            }
        }
        check(!failed, "Official corpus admission/cook failed; inspect individual diagnostics");
        std::cout << "18 pinned official fixtures passed native geometry, material, image, skin, "
                     "animation and scene admission\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
