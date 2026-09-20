#include "gltf_model_cook.hpp"
#include "asset_bytes.hpp"
#include "gltf_scene.hpp"
#include "gltf_surfaces.hpp"
#include "gltf_transform.hpp"
#include "texture_ktx.hpp"
#include <algorithm>
#include <cstring>
#include <forge/texture_bundle.hpp>
namespace forge::asset_detail {
namespace {
using Json = nlohmann::json;
void require(bool ok, const char* why) {
    if (!ok)
        throw std::runtime_error(why);
}
std::string address(std::string_view group, std::size_t index) {
    return "/" + std::string(group) + "/" + std::to_string(index);
}
std::string label(const Json& row) {
    const auto result = row.value("name", std::string{});
    require(result.size() <= 4096 && result.find('\0') == std::string::npos,
            "Model label exceeds bounds");
    return result;
}
std::string sorted_digest(std::vector<std::string> values) {
    std::sort(values.begin(), values.end());
    return asset_build_digest(values);
}
std::string mesh_evidence(const MeshData& mesh) {
    // Geometry identity excludes source material indices, primitive order and
    // human morph labels. Reference/usage evidence is handled separately.
    std::vector<std::string> parts;
    for (const auto& part : mesh.lods.at(0).parts) {
        MeshData one;
        one.morph_defaults = mesh.morph_defaults;
        for (std::size_t i = 0; i < mesh.morph_names.size(); ++i)
            one.morph_names.push_back("target-" + std::to_string(i));
        one.lods.push_back({1, {part}});
        one.lods[0].parts[0].material_slot = 0;
        parts.push_back(content_digest(encode_mesh(one)));
    }
    return sorted_digest(std::move(parts));
}
void image_binding(const GltfTextureBinding& binding, const GltfEncodedImage& image) {
    const auto bytes = image.encoded.bytes();
    const auto starts = [&](std::string_view pattern) {
        return bytes.size() >= pattern.size() &&
               std::memcmp(bytes.data(), pattern.data(), pattern.size()) == 0;
    };
    std::string_view mime;
    if (binding.image_extension == "KHR_texture_basisu") {
        require(starts(std::string_view("\xABKTX 20\xBB\r\n\x1A\n", 12)),
                "glTF Basis binding does not contain a KTX2 image");
        mime = "image/ktx2";
    } else if (binding.image_extension == "EXT_texture_webp") {
        require(starts("RIFF") && bytes.size() >= 12 &&
                    std::memcmp(bytes.data() + 8, "WEBP", 4) == 0,
                "glTF WebP binding does not contain a WebP image");
        mime = "image/webp";
    } else {
        require(binding.image_extension.empty(), "Unsupported glTF texture encoding extension");
        if (starts(std::string_view("\x89PNG\r\n\x1A\n", 8)))
            mime = "image/png";
        else if (starts(std::string_view("\xff\xd8\xff", 3)))
            mime = "image/jpeg";
        else
            throw std::runtime_error("Core glTF texture binding requires PNG or JPEG data");
    }
    require(image.mime_type.empty() || image.mime_type == mime,
            "glTF image MIME does not match encoded bytes");
}
} // namespace
std::vector<ArtifactFile> cook_gltf_geometry_bundle(const NativeGltfDocument& native,
                                                    const GltfModelCookOptions& options,
                                                    std::stop_token stop) {
    auto cancelled = [&] { require(!stop.stop_requested(), "Model cooking cancelled"); };
    cancelled();
    require(options.maximum_texture_size && options.maximum_texture_size <= 16384 &&
                unsigned(options.compression) <=
                    unsigned(TextureCompression::NativeBcHighQuality) &&
                unsigned(options.mesh.normals) <= unsigned(MeshDirections::Recalculate) &&
                unsigned(options.mesh.tangents) <= unsigned(MeshDirections::Recalculate),
            "Invalid static model processing options");
    const auto& source = native.source();
    const auto& doc = source.document;
    const bool animated = !doc.value("skins", Json::array()).empty() ||
                          !doc.value("animations", Json::array()).empty();
    const auto scene_values = gltf_scene_metadata(source);
    const auto material_variants = gltf_material_variants(source);
    const auto& meshes = doc.value("meshes", Json::array());
    const auto& materials = doc.value("materials", Json::array());
    const auto& images = doc.value("images", Json::array());
    require(meshes.size() + materials.size() + images.size() <= 4096,
            "Model members exceed import profile");
    ModelBundleIndex index;
    index.source_digest = source.source_digest;
    index.diagnostics = source.diagnostics;
    std::vector<ArtifactFile> files;
    std::uint64_t bytes = 0;
    auto append = [&](std::string name, std::vector<std::byte> data) {
        cancelled();
        require(data.size() <= 252ull * 1024 * 1024 &&
                    data.size() <= 496ull * 1024 * 1024 - bytes && files.size() < 4094,
                "Model cooked outputs exceed bounded profile");
        bytes += data.size();
        ModelMemberFile result{name, content_digest(data), data.size()};
        files.push_back({std::move(name), std::move(data)});
        return result;
    };
    std::vector<std::vector<GltfTextureBinding>> bindings;
    std::vector<std::set<TextureSemantic>> image_usages(images.size());
    std::vector<MaterialData> cooked_materials;
    for (std::size_t i = 0; i < materials.size(); ++i) {
        bindings.push_back(gltf_texture_bindings(source, i));
        cooked_materials.push_back(cook_gltf_material(native, i));
        for (const auto& binding : bindings.back()) {
            image_binding(binding, native.encoded_images().at(binding.image));
            image_usages.at(binding.image).insert(binding.semantic);
        }
    }
    std::vector<std::vector<std::string>> material_roles(materials.size());
    std::vector<std::vector<std::string>> image_roles(images.size());
    const auto& hierarchy = native.hierarchy();
    std::vector<std::string> node_context(hierarchy.nodes.size());
    std::vector<std::vector<std::string>> mesh_roles(meshes.size());
    for (const auto node_index : hierarchy.parent_first) {
        const auto& node = hierarchy.nodes[node_index];
        const auto& authored = doc.at("nodes").at(node_index);
        node_context[node_index] = asset_build_digest(
            {{"name", label(authored)},
             {"local", node.matrix},
             {"parent", node.parent == gltf_no_index ? std::string{} : node_context[node.parent]}});
        if (node.mesh != gltf_no_index)
            mesh_roles.at(node.mesh).push_back(node_context[node_index]);
    }
    for (std::size_t i = 0; i < meshes.size(); ++i) {
        cancelled();
        MeshProcessingOptions preserve;
        preserve.normals = preserve.tangents = MeshDirections::Preserve;
        preserve.weld_exact = preserve.optimize_vertex_fetch = false;
        auto prepared = cook_gltf_mesh(native, i, preserve, options.skin_influences);
        index.diagnostics.insert(index.diagnostics.end(), prepared.diagnostics.begin(),
                                 prepared.diagnostics.end());
        auto raw = std::move(prepared.mesh);
        const auto evidence = mesh_evidence(raw);
        ModelImportMember member;
        member.identity = {
            address("meshes", i),
            "mesh",
            label(meshes[i]),
            {"", evidence, mesh_roles[i].empty() ? std::string{} : sorted_digest(mesh_roles[i])}};
        auto processing = options.mesh;
        for (std::size_t p = 0; p < raw.lods[0].parts.size(); ++p) {
            const auto& part = raw.lods[0].parts[p];
            std::set<std::size_t> used_materials;
            if (part.material_slot)
                used_materials.insert(part.material_slot - 1);
            for (const auto& variant : material_variants)
                if (const auto at = variant.mappings.find({i, p}); at != variant.mappings.end())
                    used_materials.insert(at->second);
            for (const auto m : used_materials) {
                member.bindings["material." + std::to_string(m + 1)] = address("materials", m);
                material_roles.at(m).push_back(evidence);
                for (const auto& binding : bindings.at(m)) {
                    require(
                        part.find("TEXCOORD_" + std::to_string(binding.uv_set)),
                        "Model material or variant references a missing texture-coordinate stream");
                    // One stored tangent stream describes the base material. Native PBR
                    // can derive alternate frames from that material's UV gradients.
                    if (m + 1 == part.material_slot && binding.role == "normalTexture")
                        processing.tangent_uv_sets[part.material_slot] = binding.uv_set;
                }
            }
        }
        auto processed = process_mesh(raw, processing, {}, stop);
        for (auto& diagnostic : processed.diagnostics)
            index.diagnostics.push_back("Mesh " + std::to_string(i) + ": " + diagnostic);
        member.artifact =
            append("mesh-" + std::to_string(i) + ".fmesh", encode_mesh(processed.mesh));
        index.members.push_back(std::move(member));
    }
    for (std::size_t i = 0; i < materials.size(); ++i) {
        cancelled();
        ModelImportMember member;
        const auto data = encode_material(cooked_materials[i]);
        Json content{{"values", content_digest(data)}, {"textures", Json::object()}};
        for (const auto& binding : bindings[i]) {
            member.bindings[binding.role] = address("images", binding.image);
            content["textures"][binding.role] =
                content_digest(native.encoded_images().at(binding.image).encoded.bytes());
            image_roles[binding.image].push_back(asset_build_digest(
                {{"slot", binding.role}, {"material_usage", sorted_digest(material_roles[i])}}));
        }
        member.identity = {
            address("materials", i),
            "material",
            label(materials[i]),
            {"", asset_build_digest(content),
             material_roles[i].empty() ? std::string{} : sorted_digest(material_roles[i])}};
        member.artifact = append("material-" + std::to_string(i) + ".fmat", data);
        index.members.push_back(std::move(member));
    }
    TextureLimits texture_limits;
    texture_limits.bytes = 248ull * 1024 * 1024;
    for (std::size_t i = 0; i < images.size(); ++i) {
        cancelled();
        if (image_usages[i].empty()) {
            index.diagnostics.push_back("Unreferenced source image " + std::to_string(i) +
                                        " retained in source; no runtime texture generated");
            continue;
        }
        const auto& image = native.encoded_images().at(i);
        const auto input = image.encoded.bytes();
        const bool basis =
            image.mime_type == "image/ktx2" ||
            (input.size() >= 12 && std::memcmp(input.data(), "\xABKTX 20\xBB\r\n\x1A\n", 12) == 0);
        TextureBundleIndex bundle;
        bundle.primary = *image_usages[i].begin();
        for (const auto usage : image_usages[i]) {
            TextureData texture;
            if (basis)
                texture = import_texture_ktx2(
                    input, usage, options.desktop_bc ? BasisTarget::DesktopBc : BasisTarget::Rgba8,
                    true, texture_limits, stop);
            else {
                TextureImportSettings settings;
                settings.semantic = usage;
                settings.srgb = usage == TextureSemantic::Color;
                settings.compression = options.compression;
                settings.max_size = options.maximum_texture_size;
                texture = import_texture_image(input, "gltf-image", settings, texture_limits, stop);
            }
            require(texture.dimension == TextureDimension::D2, "glTF material image must be 2D");
            if (basis) {
                require(options.maximum_texture_size && options.maximum_texture_size <= 16384,
                        "Invalid model texture maximum dimension");
                unsigned skip = 0;
                while (skip < texture.mips &&
                       (std::max(1u, texture.width >> skip) > options.maximum_texture_size ||
                        std::max(1u, texture.height >> skip) > options.maximum_texture_size))
                    ++skip;
                require(skip < texture.mips,
                        "Basis image has no supplied mip within requested maximum dimension");
                texture.width = std::max(1u, texture.width >> skip);
                texture.height = std::max(1u, texture.height >> skip);
                texture.mips -= skip;
                texture.subresources.erase(texture.subresources.begin(),
                                           texture.subresources.begin() + skip);
            }
            const auto file =
                append(texture_variant_file(usage, "image-" + std::to_string(i) + "-"),
                       encode_texture(texture, texture_limits));
            bundle.variants.push_back({usage, file.file, file.digest, file.bytes});
        }
        ModelImportMember member;
        member.identity = {address("images", i),
                           "texture",
                           label(images[i]),
                           {"", content_digest(input), sorted_digest(image_roles[i])}};
        member.artifact =
            append("image-" + std::to_string(i) + ".json", encode_texture_bundle_index(bundle));
        index.members.push_back(std::move(member));
    }
    Json nodes = Json::array();
    for (std::size_t i = 0; i < hierarchy.nodes.size(); ++i) {
        const auto& node = hierarchy.nodes[i];
        std::array<double, 12> affine;
        for (unsigned row = 0; row < 3; ++row)
            for (unsigned col = 0; col < 4; ++col)
                affine[row * 4 + col] = node.matrix[col * 4 + row];
        nodes.push_back(
            {{"name", label(doc.at("nodes").at(i))},
             {"parent", node.parent == gltf_no_index ? Json(nullptr) : Json(node.parent)},
             {"mesh",
              node.mesh == gltf_no_index ? Json(nullptr) : Json(address("meshes", node.mesh))},
             {"local", affine},
             {"trs", canonical_gltf_trs(doc.at("nodes").at(i))},
             {"weights", node.morph_weights},
             {"camera", node.camera == gltf_no_index ? Json(nullptr) : Json(node.camera)},
             {"light", scene_values.nodes.at(i).at("light")},
             {"visible", scene_values.nodes.at(i).at("visible")},
             {"selectable", scene_values.nodes.at(i).at("selectable")}});
    }
    index.hierarchy = {{"nodes", nodes},
                       {"scenes", hierarchy.scenes},
                       {"default_scene", hierarchy.default_scene == gltf_no_index
                                             ? Json(nullptr)
                                             : Json(hierarchy.default_scene)}};
    index.hierarchy["cameras"] = scene_values.cameras;
    index.hierarchy["lights"] = scene_values.lights;
    auto variants = Json::array();
    for (const auto& variant : material_variants) {
        auto mappings = Json::array();
        for (const auto& [part, material] : variant.mappings)
            mappings.push_back({{"mesh", address("meshes", part.first)},
                                {"primitive", part.second},
                                {"material", address("materials", material)}});
        variants.push_back({{"name", variant.name}, {"mappings", mappings}});
    }
    index.hierarchy["material_variants"] = std::move(variants);
    if (animated)
        index.hierarchy["animation_pending"] = true;
    files.push_back({"model.json", encode_model_bundle_index(index)});
    (void)validate_model_bundle(files, ModelValidation::GeometryStage);
    cancelled();
    return files;
}
std::vector<ArtifactFile> cook_static_gltf_bundle(const NativeGltfDocument& native,
                                                  const GltfModelCookOptions& options,
                                                  std::stop_token stop) {
    for (const auto* key : {"skins", "animations"})
        require(!native.source().document.contains(key) || native.source().document.at(key).empty(),
                "Static model cooking cannot discard skin/animation stages");
    return cook_gltf_geometry_bundle(native, options, stop);
}
} // namespace forge::asset_detail
