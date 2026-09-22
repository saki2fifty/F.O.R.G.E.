#include "gltf_model_cook.hpp"
#include "asset_bytes.hpp"
#include "gltf_lod.hpp"
#include "gltf_scene.hpp"
#include "gltf_surfaces.hpp"
#include "gltf_transform.hpp"
#include "gltf_validation.hpp"
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
    const auto& source = native.scene_source();
    const auto& doc = source.document;
    const bool animated = !doc.value("skins", Json::array()).empty() ||
                          !doc.value("animations", Json::array()).empty();
    const auto scene_values = gltf_scene_metadata(source);
    const auto lod_groups = gltf_mesh_lods(native, scene_values);
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
    std::vector<std::string> node_mesh(hierarchy.nodes.size());
    for (std::size_t i = 0; i < hierarchy.nodes.size(); ++i)
        if (hierarchy.nodes[i].mesh != gltf_no_index)
            node_mesh[i] = address("meshes", hierarchy.nodes[i].mesh);
    for (const auto& group : lod_groups) {
        cancelled();
        // A source mesh may also be used by unrelated nodes. Publish a distinct
        // combined member, so that its LOD policy does not alter those uses.
        MeshData combined;
        ModelImportMember member;
        Json evidence = Json::array();
        std::size_t payload = 0, parts = 0, vertices = 0, indices = 0;
        const MeshLimits limits;
        for (std::size_t l = 0; l < group.meshes.size(); ++l) {
            const auto source_mesh = group.meshes[l];
            const auto& original = index.members.at(source_mesh);
            for (const auto& variant : material_variants)
                for (const auto& [part, material] : variant.mappings) {
                    (void)material;
                    require(part.first != source_mesh,
                            "MSFT_lod: material variants on LOD meshes require a combined variant "
                            "adapter; previous publication is retained");
                }
            const auto& artifact = files.at(source_mesh);
            require(artifact.name == original.artifact.file,
                    "LOD preparation lost its source mesh artifact");
            auto level = decode_mesh(artifact.bytes);
            require(level.lods.size() == 1, "Source mesh unexpectedly has multiple LODs");
            const auto level_bytes = level.byte_size();
            require(level_bytes <= limits.bytes - payload, "Combined LOD mesh exceeds byte budget");
            payload += level_bytes;
            for (const auto& part : level.lods[0].parts) {
                require(parts < limits.parts && part.vertices <= limits.vertices - vertices &&
                            part.indices.size() <= limits.indices - indices,
                        "Combined LOD mesh exceeds geometry budget");
                ++parts;
                vertices += part.vertices;
                indices += part.indices.size();
            }
            if (!l) {
                combined.material_slots = level.material_slots;
                combined.morph_names = level.morph_names;
                combined.morph_defaults = level.morph_defaults;
            } else
                require(combined.material_slots == level.material_slots &&
                            combined.morph_names == level.morph_names &&
                            combined.morph_defaults == level.morph_defaults,
                        "MSFT_lod: morph target order/names/defaults differ across mesh levels");
            evidence.push_back({{"geometry", original.identity.evidence.content_digest},
                                {"coverage", group.coverage[l]}});
            level.lods[0].screen_coverage = group.coverage[l];
            combined.lods.push_back(std::move(level.lods[0]));
            member.bindings.insert(original.bindings.begin(), original.bindings.end());
        }
        validate_mesh(combined);
        const auto location = address("lods", group.node);
        member.identity = {location,
                           "mesh",
                           label(doc.at("nodes").at(group.node)).substr(0, 4091) + " LODs",
                           {"", asset_build_digest(evidence), node_context[group.node]}};
        member.artifact =
            append("lod-" + std::to_string(group.node) + ".fmesh", encode_mesh(combined));
        index.members.push_back(std::move(member));
        node_mesh[group.node] = location;
        if (group.omitted_cull_hint)
            index.diagnostics.push_back(
                "MSFT_lod node " + std::to_string(group.node) +
                ": transitions use source screen-coverage hints; the optional final disappearance "
                "hint is not applied. The lowest LOD remains visible.");
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
             {"mesh", node_mesh[i].empty() ? Json(nullptr) : Json(node_mesh[i])},
             {"local", affine},
             {"trs", canonical_gltf_trs(doc.at("nodes").at(i))},
             {"weights", node.morph_weights},
             {"camera", node.camera == gltf_no_index ? Json(nullptr) : Json(node.camera)},
             {"light", scene_values.nodes.at(i).at("light")},
             {"visible", scene_values.nodes.at(i).at("visible")},
             {"selectable", scene_values.nodes.at(i).at("selectable")}});
    }
    auto scenes = hierarchy.scenes;
    auto default_scene = hierarchy.default_scene;
    if (scenes.empty() && !lod_groups.empty()) {
        // The usual no-scene fallback is every root. LOD alternatives are
        // resources of their owner, not additional placements in that fallback.
        std::set<std::size_t> alternatives;
        for (const auto& group : lod_groups)
            alternatives.insert(group.alternatives.begin(), group.alternatives.end());
        auto& roots = scenes.emplace_back();
        for (std::size_t i = 0; i < hierarchy.nodes.size(); ++i)
            if (hierarchy.nodes[i].parent == gltf_no_index && !alternatives.contains(i))
                roots.push_back(i);
        default_scene = 0;
        index.diagnostics.push_back("No explicit source scenes: implicit placement excludes lower "
                                    "MSFT_lod alternatives from ordinary root placements.");
    }
    index.hierarchy = {
        {"nodes", nodes},
        {"scenes", scenes},
        {"default_scene", default_scene == gltf_no_index ? Json(nullptr) : Json(default_scene)}};
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
    // One immutable logical member per source node. Evidence excludes array
    // positions and display labels; identical content may require an explicit
    // correspondence choice after source changes. Parent-first traversal is
    // already validated and bounds this work without recursion.
    // Target channel kinds are source-node role evidence, independent of clip
    // order/names. Full sampler/curve/skin validation still belongs to the native
    // animation stage before Complete publication; this only reads bounded roles.
    std::vector<unsigned> animation_roles(nodes.size());
    std::size_t channel_budget = 1000000;
    for (const auto& animation : gltf_detail::array(doc, "animations", 64)) {
        cancelled();
        const auto& channels = gltf_detail::array(animation, "channels", channel_budget);
        channel_budget -= channels.size();
        for (const auto& channel : channels) {
            const auto& target = channel.at("target");
            const auto at = gltf_detail::size_value(target.at("node"));
            const auto& path = target.at("path").get_ref<const std::string&>();
            const unsigned bit = path == "translation" ? 1u
                                 : path == "rotation"  ? 2u
                                 : path == "scale"     ? 4u
                                 : path == "weights"   ? 8u
                                                       : 0u;
            require(at < nodes.size() && bit != 0, "Invalid model node animation role");
            animation_roles[at] |= bit;
        }
    }
    std::vector<std::string> own(nodes.size()), role(nodes.size()), subtree(nodes.size()),
        role_subtree(nodes.size()), context(nodes.size());
    std::vector<std::vector<std::size_t>> children(nodes.size());
    std::map<std::string, std::string> member_content;
    std::map<std::string, const ModelImportMember*> mesh_members;
    for (const auto& member : index.members)
        member_content.emplace(member.identity.address, member.identity.evidence.content_digest);
    for (const auto& member : index.members)
        if (member.identity.type == "mesh")
            mesh_members.emplace(member.identity.address, &member);
    for (std::size_t i = 0; i < nodes.size(); ++i) {
        const auto& node = hierarchy.nodes[i];
        Json value = nodes[i];
        value.erase("name");
        value.erase("parent");
        value.erase("local");
        value["skin_bound"] = node.skin != gltf_no_index;
        value["mesh"] =
            node_mesh[i].empty() ? Json(nullptr) : Json(member_content.at(node_mesh[i]));
        value["camera"] =
            node.camera == gltf_no_index ? Json(nullptr) : scene_values.cameras.at(node.camera);
        value["light"] = nodes[i].at("light").is_null()
                             ? Json(nullptr)
                             : scene_values.lights.at(nodes[i].at("light").get<std::size_t>());
        std::vector<std::string> material_content;
        if (!node_mesh[i].empty())
            for (const auto& [binding, target] : mesh_members.at(node_mesh[i])->bindings) {
                (void)binding;
                material_content.push_back(member_content.at(target));
            }
        value["materials"] = sorted_digest(std::move(material_content));
        own[i] = asset_build_digest(value);
        value["animation_roles"] = animation_roles[i];
        // A transform/visibility edit must not remove all correspondence evidence.
        // Geometry plus hierarchy role remains independent from local TRS/labels.
        for (const auto* key : {"trs", "visible", "selectable", "weights", "materials"})
            value.erase(key);
        role[i] = asset_build_digest(value);
        if (node.parent != gltf_no_index)
            children[node.parent].push_back(i);
    }
    for (auto at = hierarchy.parent_first.rbegin(); at != hierarchy.parent_first.rend(); ++at) {
        std::vector<std::string> child_content, child_roles;
        for (auto child : children[*at]) {
            child_content.push_back(subtree[child]);
            child_roles.push_back(role_subtree[child]);
        }
        role_subtree[*at] = asset_build_digest(
            {{"own", role[*at]}, {"children", sorted_digest(std::move(child_roles))}});
        subtree[*at] = asset_build_digest(
            {{"own", own[*at]}, {"children", sorted_digest(std::move(child_content))}});
    }
    for (auto i : hierarchy.parent_first) {
        const auto parent = hierarchy.nodes[i].parent;
        context[i] = asset_build_digest(
            {{"role", role_subtree[i]},
             {"parent", parent == gltf_no_index ? std::string{} : context[parent]}});
    }
    for (std::size_t i = 0; i < nodes.size(); ++i) {
        ModelImportMember member;
        member.identity = {address("nodes", i),
                           "model_node",
                           nodes[i].at("name").get<std::string>(),
                           {"", subtree[i], context[i]}};
        member.node = static_cast<std::uint32_t>(i);
        if (!nodes[i].at("mesh").is_null())
            member.bindings.emplace("mesh", nodes[i].at("mesh").get<std::string>());
        index.members.push_back(std::move(member));
    }
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
        require(!native.scene_source().document.contains(key) ||
                    native.scene_source().document.at(key).empty(),
                "Static model cooking cannot discard skin/animation stages");
    return cook_gltf_geometry_bundle(native, options, stop);
}
} // namespace forge::asset_detail
